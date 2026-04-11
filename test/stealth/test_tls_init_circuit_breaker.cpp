// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/actor/actor.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/utils/common.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#include "td/mtproto/TlsInit.h"

#include "test/stealth/TlsInitTestPeer.h"

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsInitTestHelpers.h"

#include "td/utils/tests.h"

#if !TD_DARWIN

namespace {

using td::mtproto::stealth::default_runtime_platform_hints;
using td::mtproto::stealth::get_runtime_ech_counters;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::profile_spec;
using td::mtproto::stealth::reset_runtime_ech_counters_for_tests;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::find_extension;
using td::mtproto::test::make_tls_init_response;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::TlsInitTestPeer;
using td::mtproto::test::write_all;
using td::mtproto::TlsInit;

class NoopCallback final : public td::TransparentProxy::Callback {
 public:
  void set_result(td::Result<td::BufferedFd<td::SocketFd>>) final {
  }

  void on_connected() final {
  }
};

constexpr td::Slice kFirstResponsePrefix("\x16\x03\x03");
constexpr td::Slice kSecondResponsePrefix("\x14\x03\x03\x00\x01\x01\x17\x03\x03");

struct RuntimeProfileCandidate final {
  td::string domain;
  td::int32 unix_time{0};
};

RuntimeProfileCandidate find_ech_enabled_runtime_candidate() {
  auto platform = default_runtime_platform_hints();
  for (td::uint32 bucket = 20000; bucket < 20256; bucket++) {
    auto unix_time = static_cast<td::int32>(bucket * 86400 + 1800);
    for (td::uint32 i = 0; i < 256; i++) {
      td::string domain = "cb-" + td::to_string(i) + ".example.com";
      auto profile = pick_runtime_profile(domain, unix_time, platform);
      if (profile_spec(profile).allows_ech) {
        return RuntimeProfileCandidate{std::move(domain), unix_time};
      }
    }
  }
  UNREACHABLE();
  return RuntimeProfileCandidate{};
}

RuntimeProfileCandidate find_distinct_ech_enabled_runtime_candidate(td::Slice excluded_domain,
                                                                    td::int32 preferred_unix_time) {
  auto platform = default_runtime_platform_hints();
  auto preferred_bucket = static_cast<td::uint32>(preferred_unix_time / 86400);

  for (td::uint32 bucket_offset = 0; bucket_offset < 256; bucket_offset++) {
    auto bucket = preferred_bucket + bucket_offset;
    auto unix_time = static_cast<td::int32>(bucket * 86400 + 1800);
    for (td::uint32 i = 0; i < 256; i++) {
      td::string domain = "cb-alt-" + td::to_string(bucket_offset) + "-" + td::to_string(i) + ".example.com";
      if (domain == excluded_domain) {
        continue;
      }
      auto profile = pick_runtime_profile(domain, unix_time, platform);
      if (profile_spec(profile).allows_ech) {
        return RuntimeProfileCandidate{std::move(domain), unix_time};
      }
    }
  }

  UNREACHABLE();
  return RuntimeProfileCandidate{};
}

td::string flush_client_hello(TlsInit &tls_init, td::SocketFd &peer_fd) {
  auto bytes_to_read = TlsInitTestPeer::fd(tls_init).ready_for_flush_write();
  CHECK(bytes_to_read > 0);
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Write());
  while (TlsInitTestPeer::fd(tls_init).ready_for_flush_write() > 0) {
    auto flush_status = TlsInitTestPeer::fd(tls_init).flush_write();
    CHECK(flush_status.is_ok());
  }
  return td::mtproto::test::read_exact(peer_fd, bytes_to_read).move_as_ok();
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

void flush_invalid_response_and_expect_error(TlsInit &tls_init, td::SocketFd &peer_fd) {
  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, kSecondResponsePrefix);
  response[11] ^= 0x01;

  ASSERT_TRUE(write_all(peer_fd, response).is_ok());
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read().is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_error());
}

void flush_valid_response_and_expect_success(TlsInit &tls_init, td::SocketFd &peer_fd) {
  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, kSecondResponsePrefix);

  ASSERT_TRUE(write_all(peer_fd, response).is_ok());
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read().is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_ok());
}

TEST(TlsInitCircuitBreaker, RepeatedEchFailuresDisableEchForNextConnection) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();
  auto candidate = find_ech_enabled_runtime_candidate();

  for (td::int32 attempt = 0; attempt < 3; attempt++) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);

    flush_invalid_response_and_expect_error(tls_init, socket_pair.peer);
  }

  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
  TlsInitTestPeer::send_hello(tls_init);

  auto wire = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0xFE02) == nullptr);
}

TEST(TlsInitCircuitBreaker, RuntimeCountersTrackEnabledRouteDisabledCbAndReenable) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();
  auto candidate = find_ech_enabled_runtime_candidate();

  {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);
    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);
  }

  NetworkRouteHints unknown_route;
  unknown_route.is_known = false;
  unknown_route.is_ru = false;
  {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init =
        create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time, unknown_route);
    TlsInitTestPeer::send_hello(tls_init);
    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  }

  for (td::int32 attempt = 0; attempt < 3; attempt++) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);
    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    flush_invalid_response_and_expect_error(tls_init, socket_pair.peer);
  }

  {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);
    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  }

  td::Time::jump_in_future(td::Time::now() + 301.0);
  {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);
    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);
  }

  auto counters = get_runtime_ech_counters();
  ASSERT_TRUE(counters.enabled_total >= 2);
  ASSERT_EQ(1u, counters.disabled_route_total);
  ASSERT_EQ(1u, counters.disabled_cb_total);
  ASSERT_EQ(1u, counters.reenabled_total);
}

TEST(TlsInitCircuitBreaker, FailuresForOneDestinationMustNotDisableEchForDifferentDestination) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();
  auto blocked_candidate = find_ech_enabled_runtime_candidate();
  auto healthy_candidate =
      find_distinct_ech_enabled_runtime_candidate(blocked_candidate.domain, blocked_candidate.unix_time);

  for (td::int32 attempt = 0; attempt < 3; attempt++) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init =
        create_tls_init(std::move(socket_pair.client), blocked_candidate.domain, blocked_candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);

    flush_invalid_response_and_expect_error(tls_init, socket_pair.peer);
  }

  {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init =
        create_tls_init(std::move(socket_pair.client), blocked_candidate.domain, blocked_candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  }

  {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init =
        create_tls_init(std::move(socket_pair.client), healthy_candidate.domain, healthy_candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);
  }
}

TEST(TlsInitCircuitBreaker, FailuresMustNotDisableEchForNextDayBucketOfSameDestination) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();
  auto candidate = find_ech_enabled_runtime_candidate();
  auto next_bucket_unix_time = static_cast<td::int32>(candidate.unix_time + 86400);

  for (td::int32 attempt = 0; attempt < 3; attempt++) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);

    flush_invalid_response_and_expect_error(tls_init, socket_pair.peer);
  }

  {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  }

  {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, next_bucket_unix_time);
    TlsInitTestPeer::send_hello(tls_init);

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);
  }
}

TEST(TlsInitCircuitBreaker, SuccessForDifferentDestinationMustNotClearBlockedDestinationState) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();
  auto blocked_candidate = find_ech_enabled_runtime_candidate();
  auto successful_candidate =
      find_distinct_ech_enabled_runtime_candidate(blocked_candidate.domain, blocked_candidate.unix_time);

  for (td::int32 attempt = 0; attempt < 3; attempt++) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init =
        create_tls_init(std::move(socket_pair.client), blocked_candidate.domain, blocked_candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);

    flush_invalid_response_and_expect_error(tls_init, socket_pair.peer);
  }

  {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init =
        create_tls_init(std::move(socket_pair.client), successful_candidate.domain, successful_candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);

    flush_valid_response_and_expect_success(tls_init, socket_pair.peer);
  }

  {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init =
        create_tls_init(std::move(socket_pair.client), blocked_candidate.domain, blocked_candidate.unix_time);
    TlsInitTestPeer::send_hello(tls_init);

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  }
}

}  // namespace

#endif