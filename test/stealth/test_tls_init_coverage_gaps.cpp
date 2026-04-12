// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/actor/ConcurrentScheduler.h"

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/mtproto/TlsInit.h"

#include "td/net/ProxySetupError.h"

#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsInitTestHelpers.h"
#include "test/stealth/TlsInitTestPeer.h"

#include "td/utils/common.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"
#include "td/utils/tests.h"

#include "td/utils/port/config.h"

#if TD_PORT_POSIX && !TD_DARWIN

namespace {

using td::mtproto::stealth::default_runtime_platform_hints;
using td::mtproto::stealth::get_runtime_ech_counters;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::profile_spec;
using td::mtproto::stealth::reset_runtime_ech_counters_for_tests;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::test::append_u16_be;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::read_exact;
using td::mtproto::test::TlsInitTestPeer;
using td::mtproto::test::write_all;
using td::mtproto::TlsInit;

constexpr td::uint16 kEchExtensionType = td::mtproto::test::fixtures::kEchExtensionType;

class NoopCallback final : public td::TransparentProxy::Callback {
 public:
  void set_result(td::Result<td::BufferedFd<td::SocketFd>>) final {
  }

  void on_connected() final {
  }
};

struct RuntimeProfileCandidate final {
  td::string domain;
  td::int32 unix_time{0};
};

struct ActorRunObservation final {
  bool finished{false};
  bool success{false};
  td::Status error;
};

class RecordingCallback final : public td::TransparentProxy::Callback {
 public:
  explicit RecordingCallback(ActorRunObservation *observation) : observation_(observation) {
  }

  void set_result(td::Result<td::BufferedFd<td::SocketFd>> result) final {
    observation_->finished = true;
    observation_->success = result.is_ok();
    if (result.is_error()) {
      observation_->error = result.move_as_error();
    }
    td::Scheduler::instance()->finish();
  }

  void on_connected() final {
  }

 private:
  ActorRunObservation *observation_;
};

RuntimeProfileCandidate find_ech_enabled_runtime_candidate() {
  auto platform = default_runtime_platform_hints();
  for (td::uint32 bucket = 20000; bucket < 20256; bucket++) {
    auto unix_time = static_cast<td::int32>(bucket * 86400 + 1800);
    for (td::uint32 i = 0; i < 256; i++) {
      td::string domain = "coverage-gap-" + td::to_string(i) + ".example.com";
      auto profile = pick_runtime_profile(domain, unix_time, platform);
      if (profile_spec(profile).allows_ech) {
        return RuntimeProfileCandidate{std::move(domain), unix_time};
      }
    }
  }
  UNREACHABLE();
  return RuntimeProfileCandidate{};
}

TlsInit create_tls_init(td::SocketFd socket_fd) {
  reset_runtime_ech_failure_state_for_tests();
  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;
  return TlsInit(std::move(socket_fd), "www.google.com", "0123456789secret", td::make_unique<NoopCallback>(), {},
                 0.0, route_hints);
}

TlsInit create_tls_init(td::SocketFd socket_fd, td::Slice domain, td::int32 unix_time) {
  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;
  auto server_time_difference = static_cast<double>(unix_time) - td::Time::now();
  return TlsInit(std::move(socket_fd), domain.str(), "0123456789secret", td::make_unique<NoopCallback>(), {},
                 server_time_difference, route_hints);
}

TlsInit create_tls_init(td::SocketFd socket_fd, td::Slice domain, td::int32 unix_time,
                        const NetworkRouteHints &route_hints) {
  auto server_time_difference = static_cast<double>(unix_time) - td::Time::now();
  return TlsInit(std::move(socket_fd), domain.str(), "0123456789secret", td::make_unique<NoopCallback>(), {},
                 server_time_difference, route_hints);
}

td::Status flush_response_into_tls_init(TlsInit &tls_init, td::SocketFd &peer_fd, td::Slice response) {
  TRY_STATUS(write_all(peer_fd, response));
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  TRY_RESULT(read_size, TlsInitTestPeer::fd(tls_init).flush_read());
  (void)read_size;
  return td::Status::OK();
}

td::string flush_client_hello(TlsInit &tls_init, td::SocketFd &peer_fd) {
  auto bytes_to_read = TlsInitTestPeer::fd(tls_init).ready_for_flush_write();
  CHECK(bytes_to_read > 0);
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Write());
  while (TlsInitTestPeer::fd(tls_init).ready_for_flush_write() > 0) {
    auto flush_status = TlsInitTestPeer::fd(tls_init).flush_write();
    CHECK(flush_status.is_ok());
  }
  return read_exact(peer_fd, bytes_to_read).move_as_ok();
}

td::string make_tls_record(td::uint8 record_type, td::Slice payload) {
  td::string record;
  record.push_back(static_cast<char>(record_type));
  record.push_back('\x03');
  record.push_back('\x03');
  append_u16_be(record, static_cast<td::uint16>(payload.size()));
  record += payload.str();
  return record;
}

td::string make_short_complete_response() {
  return make_tls_record(0x16, "\x42") + make_tls_record(0x17, "\x24");
}

td::string read_full_tls_record(td::SocketFd &peer_fd) {
  auto header = read_exact(peer_fd, 5).move_as_ok();
  auto record_length = (static_cast<td::uint16>(static_cast<td::uint8>(header[3])) << 8) |
                       static_cast<td::uint16>(static_cast<td::uint8>(header[4]));
  return header + read_exact(peer_fd, record_length).move_as_ok();
}

NetworkRouteHints unknown_route() {
  NetworkRouteHints route_hints;
  route_hints.is_known = false;
  route_hints.is_ru = false;
  return route_hints;
}

template <class InteractT>
ActorRunObservation run_tls_init_actor(InteractT &&interact, td::Slice domain = "www.google.com",
                                       td::int32 unix_time = 0) {
  auto socket_pair = create_socket_pair().move_as_ok();
  ActorRunObservation observation;
  td::ConcurrentScheduler scheduler(0, 0);

  {
    auto guard = scheduler.get_main_guard();
    NetworkRouteHints route_hints;
    route_hints.is_known = true;
    route_hints.is_ru = false;
    auto server_time_difference = static_cast<double>(unix_time) - td::Time::now();
    td::create_actor<td::mtproto::TlsInit>("TlsInit", std::move(socket_pair.client), domain.str(), "0123456789secret",
                                           td::make_unique<RecordingCallback>(&observation), td::ActorShared<>(),
                                           server_time_difference, route_hints)
        .release();
  }

  scheduler.start();
  interact(scheduler, socket_pair.peer, observation);

  td::int32 iterations = 0;
  while (scheduler.run_main(10)) {
    iterations++;
    ASSERT_TRUE(iterations < 1000);
  }
  scheduler.finish();
  return observation;
}

TEST(TlsInitCoverageGaps, RejectsNonHandshakeFirstRecordAcrossValidTlsRecordTypes) {
  constexpr td::uint8 kRecordTypes[] = {0x14, 0x15, 0x17};

  for (auto record_type : kRecordTypes) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client));
    TlsInitTestPeer::send_hello(tls_init);

    auto response = make_tls_record(record_type, "\x42");
    ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, response).is_ok());

    auto status = TlsInitTestPeer::wait_hello_response(tls_init);
    ASSERT_TRUE(status.is_error());
    ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse), status.code());
  }
}

TEST(TlsInitCoverageGaps, NearHttpPrefixVariantsFailClosedAsWrongRegime) {
  const td::string prefixes[] = {
      td::string("HXTZ?", 5),
      td::string("HTXZ?", 5),
      td::string("HTTQ?", 5),
      td::string("HTTP?", 5),
  };

  for (const auto &prefix : prefixes) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client));
    TlsInitTestPeer::send_hello(tls_init);

    ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, prefix).is_ok());
    auto status = TlsInitTestPeer::wait_hello_response(tls_init);
    ASSERT_TRUE(status.is_error());
    ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloWrongRegime), status.code());
  }
}

TEST(TlsInitCoverageGaps, ShortCompleteResponseFailsClosedAndDisablesEchAfterRepeatedAttempts) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();
  auto candidate = find_ech_enabled_runtime_candidate();

  for (td::int32 attempt = 0; attempt < 3; attempt++) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);

    auto hello = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(hello);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(td::mtproto::test::find_extension(parsed.ok(), kEchExtensionType) != nullptr);

    ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, make_short_complete_response()).is_ok());
    auto status = TlsInitTestPeer::wait_hello_response(tls_init);
    ASSERT_TRUE(status.is_error());
    ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse), status.code());
  }

  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
  TlsInitTestPeer::send_hello(tls_init);

  auto hello = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(hello);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(td::mtproto::test::find_extension(parsed.ok(), kEchExtensionType) == nullptr);

  auto counters = get_runtime_ech_counters();
  ASSERT_TRUE(counters.enabled_total >= 3);
  ASSERT_EQ(1u, counters.disabled_cb_total);
}

TEST(TlsInitCoverageGaps, UnknownRouteWrongRegimeResponseFailsClosedWithoutEch) {
  reset_runtime_ech_failure_state_for_tests();
  auto candidate = find_ech_enabled_runtime_candidate();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time, unknown_route());
  TlsInitTestPeer::send_hello(tls_init);

  auto hello = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(hello);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(td::mtproto::test::find_extension(parsed.ok(), kEchExtensionType) == nullptr);

  ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, "HTTP/").is_ok());
  auto status = TlsInitTestPeer::wait_hello_response(tls_init);
  ASSERT_TRUE(status.is_error());
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloWrongRegime), status.code());
}

TEST(TlsInitCoverageGaps, UnknownRouteShortCompleteResponseFailsClosedWithoutEch) {
  reset_runtime_ech_failure_state_for_tests();
  auto candidate = find_ech_enabled_runtime_candidate();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time, unknown_route());
  TlsInitTestPeer::send_hello(tls_init);

  auto hello = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(hello);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(td::mtproto::test::find_extension(parsed.ok(), kEchExtensionType) == nullptr);

  ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, make_short_complete_response()).is_ok());
  auto status = TlsInitTestPeer::wait_hello_response(tls_init);
  ASSERT_TRUE(status.is_error());
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse), status.code());
}

TEST(TlsInitCoverageGaps, UnknownRouteHashMismatchFailsClosedWithoutEch) {
  reset_runtime_ech_failure_state_for_tests();
  auto candidate = find_ech_enabled_runtime_candidate();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time, unknown_route());
  TlsInitTestPeer::send_hello(tls_init);

  auto hello = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(hello);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(td::mtproto::test::find_extension(parsed.ok(), kEchExtensionType) == nullptr);

  auto response = td::mtproto::test::make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init),
                                                            "\x16\x03\x03", "\x14\x03\x03\x00\x01\x01\x17\x03\x03");
  response[11] ^= 0x01;
  ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, response).is_ok());
  auto status = TlsInitTestPeer::wait_hello_response(tls_init);
  ASSERT_TRUE(status.is_error());
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloResponseHashMismatch), status.code());
}

TEST(TlsInitCoverageGaps, UnknownRouteSuccessfulResponseSucceedsWithoutEch) {
  reset_runtime_ech_failure_state_for_tests();
  auto candidate = find_ech_enabled_runtime_candidate();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time, unknown_route());
  TlsInitTestPeer::send_hello(tls_init);

  auto hello = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(hello);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(td::mtproto::test::find_extension(parsed.ok(), kEchExtensionType) == nullptr);

  auto response = td::mtproto::test::make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init),
                                                            "\x16\x03\x03", "\x14\x03\x03\x00\x01\x01\x17\x03\x03");
  ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, response).is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_ok());
}

TEST(TlsInitCoverageGaps, ActorSuccessPathExercisesLoopImplAndCompletes) {
  auto observation = run_tls_init_actor([](td::ConcurrentScheduler &scheduler, td::SocketFd &peer_fd,
                                           ActorRunObservation &) {
    scheduler.run_main(10);
    auto hello = read_full_tls_record(peer_fd);
    ASSERT_TRUE(hello.size() >= 43);
    auto hello_rand = hello.substr(11, 32);

    auto response = td::mtproto::test::make_tls_init_response("0123456789secret", hello_rand, "\x16\x03\x03",
                                                              "\x14\x03\x03\x00\x01\x01\x17\x03\x03");
    ASSERT_TRUE(write_all(peer_fd, response).is_ok());
  });

  ASSERT_TRUE(observation.finished);
  ASSERT_TRUE(observation.success);
}

TEST(TlsInitCoverageGaps, ActorMalformedFirstRecordExercisesLoopImplErrorPath) {
  auto observation = run_tls_init_actor([](td::ConcurrentScheduler &scheduler, td::SocketFd &peer_fd,
                                           ActorRunObservation &) {
    scheduler.run_main(10);
    auto hello = read_full_tls_record(peer_fd);
    ASSERT_TRUE(hello.size() >= 43);

    ASSERT_TRUE(write_all(peer_fd, make_tls_record(0x17, "\x42")).is_ok());
  });

  ASSERT_TRUE(observation.finished);
  ASSERT_FALSE(observation.success);
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse), observation.error.code());
}

}  // namespace

#endif  // TD_PORT_POSIX && !TD_DARWIN