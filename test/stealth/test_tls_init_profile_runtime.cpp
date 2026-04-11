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

#include <unordered_set>

#if !TD_DARWIN

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::default_runtime_platform_hints;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::profile_spec;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::find_extension;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::read_exact;
using td::mtproto::test::TlsInitTestPeer;
using td::mtproto::TlsInit;

class NoopCallback final : public td::TransparentProxy::Callback {
 public:
  void set_result(td::Result<td::BufferedFd<td::SocketFd>>) final {
  }

  void on_connected() final {
  }
};

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

struct RuntimeProfileCandidate final {
  td::string domain;
  td::int32 unix_time{0};
  BrowserProfile profile{BrowserProfile::Chrome133};
};

RuntimeProfileCandidate find_chromium_runtime_candidate() {
  auto platform = default_runtime_platform_hints();
  for (td::uint32 bucket = 20000; bucket < 20256; bucket++) {
    auto unix_time = static_cast<td::int32>(bucket * 86400 + 3600);
    for (td::uint32 i = 0; i < 128; i++) {
      td::string domain = "runtime-" + td::to_string(i) + ".example.com";
      auto profile = pick_runtime_profile(domain, unix_time, platform);
      if (profile == BrowserProfile::Chrome131 || profile == BrowserProfile::Chrome120) {
        return RuntimeProfileCandidate{std::move(domain), unix_time, profile};
      }
    }
  }
  UNREACHABLE();
  return RuntimeProfileCandidate{};
}

void assert_profile_shape(const td::mtproto::test::ParsedClientHello &hello, BrowserProfile profile, bool expect_ech) {
  auto &spec = profile_spec(profile);

  if (spec.alps_type != 0) {
    ASSERT_TRUE(find_extension(hello, spec.alps_type) != nullptr);
  }
  if (spec.alps_type != td::mtproto::test::fixtures::kAlpsChrome131) {
    ASSERT_TRUE(find_extension(hello, td::mtproto::test::fixtures::kAlpsChrome131) == nullptr ||
                spec.alps_type == td::mtproto::test::fixtures::kAlpsChrome131);
  }
  if (spec.alps_type != td::mtproto::test::fixtures::kAlpsChrome133Plus) {
    ASSERT_TRUE(find_extension(hello, td::mtproto::test::fixtures::kAlpsChrome133Plus) == nullptr ||
                spec.alps_type == td::mtproto::test::fixtures::kAlpsChrome133Plus);
  }

  std::unordered_set<td::uint16> supported_groups(hello.supported_groups.begin(), hello.supported_groups.end());
  std::unordered_set<td::uint16> key_share_groups(hello.key_share_groups.begin(), hello.key_share_groups.end());
  if (spec.has_pq) {
    ASSERT_TRUE(supported_groups.count(spec.pq_group_id) != 0);
    ASSERT_TRUE(key_share_groups.count(spec.pq_group_id) != 0);
  } else {
    ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) == 0);
    ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridDraftGroup) == 0);
    ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) == 0);
    ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridDraftGroup) == 0);
  }

  ASSERT_EQ(expect_ech, find_extension(hello, td::mtproto::test::fixtures::kEchExtensionType) != nullptr);
}

TEST(TlsInitProfileRuntime, KnownNonRuRouteUsesSelectedRuntimeChromiumProfile) {
  reset_runtime_ech_failure_state_for_tests();
  auto candidate = find_chromium_runtime_candidate();
  auto socket_pair = create_socket_pair().move_as_ok();

  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;

  auto server_time_difference = static_cast<double>(candidate.unix_time) - td::Time::now();
  TlsInit tls_init(std::move(socket_pair.client), candidate.domain, "0123456789secret", td::make_unique<NoopCallback>(),
                   {}, server_time_difference, route_hints);
  TlsInitTestPeer::send_hello(tls_init);

  auto wire = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  assert_profile_shape(parsed.ok(), candidate.profile, true);
}

TEST(TlsInitProfileRuntime, UnknownRouteKeepsSelectedRuntimeProfileButSuppressesEch) {
  reset_runtime_ech_failure_state_for_tests();
  auto candidate = find_chromium_runtime_candidate();
  auto socket_pair = create_socket_pair().move_as_ok();

  NetworkRouteHints route_hints;
  route_hints.is_known = false;
  route_hints.is_ru = false;

  auto server_time_difference = static_cast<double>(candidate.unix_time) - td::Time::now();
  TlsInit tls_init(std::move(socket_pair.client), candidate.domain, "0123456789secret", td::make_unique<NoopCallback>(),
                   {}, server_time_difference, route_hints);
  TlsInitTestPeer::send_hello(tls_init);

  auto wire = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  assert_profile_shape(parsed.ok(), candidate.profile, false);
}

}  // namespace

#endif