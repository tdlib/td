// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/tests.h"

namespace {

using td::int32;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_runtime_tls_client_hello;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::runtime_ech_mode_for_route;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthRuntimeParams;
using td::mtproto::stealth::TransportConfidence;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_ech_failure_state_for_tests();
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

NetworkRouteHints ru_route() {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = true;
  return route;
}

NetworkRouteHints non_ru_route() {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;
  return route;
}

TEST(TlsRuntimeTransportConfidenceUnknownStress, LongRunUnknownConfidenceRemainsTlsOnlyAndRuEchDisabled) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams windows_params;
  windows_params.transport_confidence = TransportConfidence::Unknown;
  windows_params.platform_hints = windows_platform();
  windows_params.profile_weights.chrome147_windows = 200;
  windows_params.profile_weights.firefox149_windows = 1;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(windows_params).is_ok());

  for (td::uint32 i = 0; i < 2000; i++) {
    auto unix_time = static_cast<int32>(1712345678 + static_cast<int32>(i * 23));
    td::string domain = "stress-unknown-win-" + td::to_string(i) + ".example";

    auto profile = pick_runtime_profile(domain, unix_time, windows_params.platform_hints);
    ASSERT_TRUE(profile == BrowserProfile::Firefox149_Windows);

    ASSERT_TRUE(runtime_ech_mode_for_route(domain, unix_time, ru_route()) == EchMode::Disabled);

    MockRng rng(0x73000000u + static_cast<td::uint64>(i));
    auto wire = build_runtime_tls_client_hello(domain, "0123456789secret", unix_time, non_ru_route(), rng);
    auto parsed_result = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_result.is_ok());
  }

  StealthRuntimeParams ios_params;
  ios_params.transport_confidence = TransportConfidence::Unknown;
  ios_params.platform_hints = ios_platform();
  ios_params.profile_weights.ios14 = 1;
  ios_params.profile_weights.chrome147_ios_chromium = 200;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(ios_params).is_ok());

  for (td::uint32 i = 0; i < 2000; i++) {
    auto unix_time = static_cast<int32>(1712345678 + static_cast<int32>(i * 29));
    td::string domain = "stress-unknown-ios-" + td::to_string(i) + ".example";

    auto profile = pick_runtime_profile(domain, unix_time, ios_params.platform_hints);
    // iOS at Unknown confidence now exposes both the advisory IOS14 lane and the
    // verified Apple iOS TLS lane (both TlsOnly).
    ASSERT_TRUE(profile == BrowserProfile::IOS14 || profile == BrowserProfile::AppleIosTls);

    ASSERT_TRUE(runtime_ech_mode_for_route(domain, unix_time, ru_route()) == EchMode::Disabled);

    MockRng rng(0x74000000u + static_cast<td::uint64>(i));
    auto wire = build_runtime_tls_client_hello(domain, "0123456789secret", unix_time, non_ru_route(), rng);
    auto parsed_result = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_result.is_ok());
  }
}

}  // namespace
