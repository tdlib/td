//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
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

#define private public
#define protected public
#include "td/mtproto/TlsInit.h"
#undef protected
#undef private

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

td::string flush_client_hello(TlsInit &tls_init, td::SocketFd &peer_fd) {
  auto bytes_to_read = tls_init.fd_.ready_for_flush_write();
  CHECK(bytes_to_read > 0);
  tls_init.fd_.get_poll_info().add_flags(td::PollFlags::Write());
  while (tls_init.fd_.ready_for_flush_write() > 0) {
    auto flush_status = tls_init.fd_.flush_write();
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
      make_tls_init_response("0123456789secret", tls_init.hello_rand_, kFirstResponsePrefix, kSecondResponsePrefix);
  response[11] ^= 0x01;

  ASSERT_TRUE(write_all(peer_fd, response).is_ok());
  tls_init.fd_.get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(tls_init.fd_.flush_read().is_ok());
  ASSERT_TRUE(tls_init.wait_hello_response().is_error());
}

TEST(TlsInitCircuitBreaker, RepeatedEchFailuresDisableEchForNextConnection) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();
  auto candidate = find_ech_enabled_runtime_candidate();

  for (td::int32 attempt = 0; attempt < 3; attempt++) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
    tls_init.send_hello();

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);

    flush_invalid_response_and_expect_error(tls_init, socket_pair.peer);
  }

  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
  tls_init.send_hello();

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
    tls_init.send_hello();
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
    tls_init.send_hello();
    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  }

  for (td::int32 attempt = 0; attempt < 3; attempt++) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
    tls_init.send_hello();
    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    flush_invalid_response_and_expect_error(tls_init, socket_pair.peer);
  }

  {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
    tls_init.send_hello();
    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  }

  td::Time::jump_in_future(td::Time::now() + 301.0);
  {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), candidate.domain, candidate.unix_time);
    tls_init.send_hello();
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

}  // namespace

#endif