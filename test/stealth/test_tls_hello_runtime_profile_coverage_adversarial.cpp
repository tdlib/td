// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/actor/actor.h"  // IWYU pragma: keep
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/utils/common.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/port/SocketFd.h"
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

#include <unordered_set>

#if !TD_DARWIN

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::find_extension;
using td::mtproto::test::fixtures::kAlpsChrome131;
using td::mtproto::test::fixtures::kAlpsChrome133Plus;
using td::mtproto::test::fixtures::kEchExtensionType;
using td::mtproto::test::fixtures::kPqHybridGroup;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::read_exact;
using td::mtproto::TlsInit;

class NoopCallback final : public td::TransparentProxy::Callback {
 public:
  void set_result(td::Result<td::BufferedFd<td::SocketFd>>) final {
  }

  void on_connected() final {
  }
};

RuntimePlatformHints make_non_darwin_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Linux;
  return platform;
}

td::Slice http11_only_alpn_body() {
  static const td::string value("\x00\x09\x08\x68\x74\x74\x70\x2f\x31\x2e\x31", 11);
  return value;
}

td::string flush_client_hello(TlsInit &tls_init, td::SocketFd &peer_fd) {
  auto bytes_to_read = tls_init.fd_.ready_for_flush_write();
  CHECK(bytes_to_read > 0);
  tls_init.fd_.get_poll_info().add_flags(td::PollFlags::Write());
  while (tls_init.fd_.ready_for_flush_write() > 0) {
    auto flush_status = tls_init.fd_.flush_write();
    CHECK(flush_status.is_ok());
  }
  return read_exact(peer_fd, bytes_to_read).move_as_ok();
}

struct RuntimeProfileCandidate final {
  td::string domain;
  td::int32 unix_time{0};
};

RuntimeProfileCandidate find_runtime_candidate(BrowserProfile wanted_profile) {
  auto platform = make_non_darwin_platform();
  for (td::uint32 bucket = 20000; bucket < 20384; bucket++) {
    auto unix_time = static_cast<td::int32>(bucket * 86400 + 3600);
    for (td::uint32 index = 0; index < 256; index++) {
      td::string domain = "runtime-profile-" + td::to_string(static_cast<td::uint32>(wanted_profile)) + "-" +
                          td::to_string(bucket) + "-" + td::to_string(index) + ".example.com";
      if (pick_runtime_profile(domain, unix_time, platform) == wanted_profile) {
        return {std::move(domain), unix_time};
      }
    }
  }
  UNREACHABLE();
  return RuntimeProfileCandidate{};
}

bool has_supported_group(const td::mtproto::test::ParsedClientHello &hello, td::uint16 group) {
  for (auto supported_group : hello.supported_groups) {
    if (supported_group == group) {
      return true;
    }
  }
  return false;
}

bool has_key_share_group(const td::mtproto::test::ParsedClientHello &hello, td::uint16 group) {
  for (auto key_share_group : hello.key_share_groups) {
    if (key_share_group == group) {
      return true;
    }
  }
  return false;
}

void assert_firefox_runtime_proxy_shape(const td::mtproto::test::ParsedClientHello &hello, bool expect_ech) {
  auto *alpn = find_extension(hello, 0x0010);
  ASSERT_TRUE(alpn != nullptr);
  ASSERT_EQ(http11_only_alpn_body(), alpn->value);

  ASSERT_TRUE(find_extension(hello, kAlpsChrome131) == nullptr);
  ASSERT_TRUE(find_extension(hello, kAlpsChrome133Plus) == nullptr);
  ASSERT_TRUE(find_extension(hello, 0x001C) != nullptr);
  ASSERT_EQ(expect_ech, find_extension(hello, kEchExtensionType) != nullptr);
  ASSERT_TRUE(has_supported_group(hello, kPqHybridGroup));
  ASSERT_TRUE(has_key_share_group(hello, kPqHybridGroup));
}

TEST(TlsHelloRuntimeProfileCoverageAdversarial, DefaultNonDarwinDesktopSelectionCoversAllAllowedProfiles) {
  auto platform = make_non_darwin_platform();

  std::unordered_set<int> seen_profiles;
  bool done = false;
  for (td::uint32 bucket = 20000; bucket < 20384 && !done; bucket++) {
    auto unix_time = static_cast<td::int32>(bucket * 86400 + 3600);
    for (td::uint32 index = 0; index < 256; index++) {
      td::string domain = "runtime-coverage-" + td::to_string(bucket) + "-" + td::to_string(index) + ".example.com";
      auto profile = pick_runtime_profile(domain, unix_time, platform);
      ASSERT_TRUE(profile != BrowserProfile::Safari26_3);
      ASSERT_TRUE(profile != BrowserProfile::IOS14);
      ASSERT_TRUE(profile != BrowserProfile::Android11_OkHttp_Advisory);
      seen_profiles.insert(static_cast<int>(profile));
      if (seen_profiles.size() == 4u) {
        done = true;
        break;
      }
    }
  }

  ASSERT_EQ(4u, seen_profiles.size());
  ASSERT_TRUE(seen_profiles.count(static_cast<int>(BrowserProfile::Chrome133)) != 0);
  ASSERT_TRUE(seen_profiles.count(static_cast<int>(BrowserProfile::Chrome131)) != 0);
  ASSERT_TRUE(seen_profiles.count(static_cast<int>(BrowserProfile::Chrome120)) != 0);
  ASSERT_TRUE(seen_profiles.count(static_cast<int>(BrowserProfile::Firefox148)) != 0);
}

TEST(TlsHelloRuntimeProfileCoverageAdversarial, KnownNonRuRouteCanEmitFirefoxRuntimeProfile) {
  reset_runtime_ech_failure_state_for_tests();
  auto candidate = find_runtime_candidate(BrowserProfile::Firefox148);
  auto socket_pair = create_socket_pair().move_as_ok();

  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;

  auto server_time_difference = static_cast<double>(candidate.unix_time) - td::Time::now();
  TlsInit tls_init(std::move(socket_pair.client), candidate.domain, "0123456789secret", td::make_unique<NoopCallback>(),
                   {}, server_time_difference, route_hints);
  tls_init.send_hello();

  auto wire = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  assert_firefox_runtime_proxy_shape(parsed.ok(), true);
}

TEST(TlsHelloRuntimeProfileCoverageAdversarial, UnknownRouteSuppressesEchForFirefoxRuntimeProfile) {
  reset_runtime_ech_failure_state_for_tests();
  auto candidate = find_runtime_candidate(BrowserProfile::Firefox148);
  auto socket_pair = create_socket_pair().move_as_ok();

  NetworkRouteHints route_hints;
  route_hints.is_known = false;
  route_hints.is_ru = false;

  auto server_time_difference = static_cast<double>(candidate.unix_time) - td::Time::now();
  TlsInit tls_init(std::move(socket_pair.client), candidate.domain, "0123456789secret", td::make_unique<NoopCallback>(),
                   {}, server_time_difference, route_hints);
  tls_init.send_hello();

  auto wire = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  assert_firefox_runtime_proxy_shape(parsed.ok(), false);
}

}  // namespace

#endif