// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

#if !TD_DARWIN

namespace {

using td::int32;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_runtime_tls_client_hello;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::reset_runtime_ech_counters_for_tests;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthRuntimeParams;
using td::mtproto::stealth::TransportConfidence;
using td::mtproto::test::baselines::get_baseline;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::Slice;

constexpr td::Slice kSecret = "0123456789secret";
constexpr int32 kUnixTimeBase = 1712345678;
constexpr double kWireLengthTolerancePercent = 15.0;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }
};

RuntimePlatformHints windows_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Windows;
  return platform;
}

RuntimePlatformHints ios_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  platform.mobile_os = MobileOs::IOS;
  return platform;
}

NetworkRouteHints non_ru_route() {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;
  return route;
}

TEST(TlsRuntimeTransportConfidenceUnknownIntegration, WindowsRuntimeWireStaysInsideFirefoxWindowsFamilyLane) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.transport_confidence = TransportConfidence::Unknown;
  params.platform_hints = windows_platform();
  params.profile_weights.chrome147_windows = 99;
  params.profile_weights.firefox149_windows = 1;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto *baseline = get_baseline(Slice("firefox_windows"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (td::uint32 i = 0; i < 256; i++) {
    auto unix_time = static_cast<int32>(kUnixTimeBase + i * 149);
    td::string domain = "unknown-integration-win-" + td::to_string(i) + ".example.com";

    auto picked = pick_runtime_profile(domain, unix_time, params.platform_hints);
    ASSERT_TRUE(picked == BrowserProfile::Firefox149_Windows);

    MockRng rng(0x71000000u + static_cast<td::uint64>(i));
    auto wire = build_runtime_tls_client_hello(domain, kSecret, unix_time, non_ru_route(), rng);
    auto parsed_result = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_result.is_ok());
    auto parsed = parsed_result.move_as_ok();

    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed));
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
  }
}

TEST(TlsRuntimeTransportConfidenceUnknownIntegration, IosRuntimeWireStaysInsideAppleTlsFamilyLane) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.transport_confidence = TransportConfidence::Unknown;
  params.platform_hints = ios_platform();
  params.profile_weights.ios14 = 1;
  params.profile_weights.chrome147_ios_chromium = 99;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto *baseline = get_baseline(Slice("apple_ios_tls"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (td::uint32 i = 0; i < 256; i++) {
    auto unix_time = static_cast<int32>(kUnixTimeBase + i * 151);
    td::string domain = "unknown-integration-ios-" + td::to_string(i) + ".example.com";

    auto picked = pick_runtime_profile(domain, unix_time, params.platform_hints);
    // iOS at Unknown confidence now exposes two TlsOnly lanes: the advisory IOS14
    // lane and the verified Apple iOS TLS lane (the closed release-grade gap).
    ASSERT_TRUE(picked == BrowserProfile::IOS14 || picked == BrowserProfile::AppleIosTls);

    MockRng rng(0x72000000u + static_cast<td::uint64>(i));
    auto wire = build_runtime_tls_client_hello(domain, kSecret, unix_time, non_ru_route(), rng);
    auto parsed_result = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_result.is_ok());
    auto parsed = parsed_result.move_as_ok();

    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed));
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
  }
}

}  // namespace

#endif
