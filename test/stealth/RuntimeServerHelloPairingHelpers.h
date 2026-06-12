// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "test/stealth/ServerHelloFixtureLoader.h"
#include "test/stealth/TestHelpers.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"

namespace td {
namespace mtproto {
namespace test {

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    stealth::reset_runtime_ech_failure_state_for_tests();
    stealth::reset_runtime_ech_counters_for_tests();
    stealth::reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    stealth::reset_runtime_ech_failure_state_for_tests();
    stealth::reset_runtime_ech_counters_for_tests();
    stealth::reset_runtime_stealth_params_for_tests();
  }
};

inline stealth::RuntimePlatformHints windows_platform() {
  stealth::RuntimePlatformHints platform;
  platform.device_class = stealth::DeviceClass::Desktop;
  platform.desktop_os = stealth::DesktopOs::Windows;
  return platform;
}

inline stealth::RuntimePlatformHints linux_platform() {
  stealth::RuntimePlatformHints platform;
  platform.device_class = stealth::DeviceClass::Desktop;
  platform.desktop_os = stealth::DesktopOs::Linux;
  return platform;
}

inline stealth::RuntimePlatformHints darwin_platform() {
  stealth::RuntimePlatformHints platform;
  platform.device_class = stealth::DeviceClass::Desktop;
  platform.desktop_os = stealth::DesktopOs::Darwin;
  return platform;
}

inline stealth::RuntimePlatformHints ios_platform() {
  stealth::RuntimePlatformHints platform;
  platform.device_class = stealth::DeviceClass::Mobile;
  platform.mobile_os = stealth::MobileOs::IOS;
  return platform;
}

inline stealth::RuntimePlatformHints android_platform() {
  stealth::RuntimePlatformHints platform;
  platform.device_class = stealth::DeviceClass::Mobile;
  platform.mobile_os = stealth::MobileOs::Android;
  return platform;
}

inline stealth::NetworkRouteHints non_ru_route() {
  stealth::NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;
  return route;
}

inline stealth::NetworkRouteHints ru_route() {
  stealth::NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = true;
  return route;
}

inline stealth::ProfileWeights zero_profile_weights() {
  stealth::ProfileWeights weights;
  weights.chrome133 = 0;
  weights.chrome131 = 0;
  weights.chrome120 = 0;
  weights.chromium_macos_no_alps = 0;
  weights.chromium_macos_4469 = 0;
  weights.chromium_macos_44cd = 0;
  weights.chrome147_windows = 0;
  weights.chrome147_ios_chromium = 0;
  weights.firefox148 = 0;
  weights.firefox149_android = 0;
  weights.firefox149_macos26_3 = 0;
  weights.firefox149_windows = 0;
  weights.safari26_3 = 0;
  weights.ios14 = 0;
  weights.android_chromium_alps = 0;
  weights.android11_okhttp_advisory = 0;
  return weights;
}

inline stealth::StealthRuntimeParams single_runtime_profile_params(stealth::BrowserProfile profile,
                                                                   stealth::TransportConfidence confidence) {
  auto params = stealth::default_runtime_stealth_params();
  params.transport_confidence = confidence;
  params.profile_weights = zero_profile_weights();

  switch (profile) {
    case stealth::BrowserProfile::Chrome133:
      params.platform_hints = linux_platform();
      params.profile_weights.chrome133 = 100;
      break;
    case stealth::BrowserProfile::Chrome131:
      params.platform_hints = linux_platform();
      params.profile_weights.chrome131 = 100;
      break;
    case stealth::BrowserProfile::Chrome120:
      params.platform_hints = linux_platform();
      params.profile_weights.chrome120 = 100;
      break;
    case stealth::BrowserProfile::Chrome147_Windows:
      params.platform_hints = windows_platform();
      params.profile_weights.chrome147_windows = 100;
      break;
    case stealth::BrowserProfile::Firefox149_Windows:
      params.platform_hints = windows_platform();
      params.profile_weights.firefox149_windows = 100;
      break;
    case stealth::BrowserProfile::Firefox148:
      params.platform_hints = linux_platform();
      params.profile_weights.firefox148 = 100;
      break;
    case stealth::BrowserProfile::Firefox149_MacOS26_3:
      params.platform_hints = darwin_platform();
      params.profile_weights.firefox149_macos26_3 = 100;
      break;
    case stealth::BrowserProfile::ChromiumMacOS_NoAlps:
      params.platform_hints = darwin_platform();
      params.profile_weights.chromium_macos_no_alps = 100;
      break;
    case stealth::BrowserProfile::ChromiumMacOS_4469:
      params.platform_hints = darwin_platform();
      params.profile_weights.chromium_macos_4469 = 100;
      break;
    case stealth::BrowserProfile::ChromiumMacOS_44CD:
      params.platform_hints = darwin_platform();
      params.profile_weights.chromium_macos_44cd = 100;
      break;
    case stealth::BrowserProfile::Chrome147_IOSChromium:
      params.platform_hints = ios_platform();
      params.profile_weights.chrome147_ios_chromium = 100;
      break;
    case stealth::BrowserProfile::Safari26_3:
      params.platform_hints = darwin_platform();
      params.profile_weights.safari26_3 = 100;
      break;
    case stealth::BrowserProfile::IOS14:
      params.platform_hints = ios_platform();
      params.profile_weights.ios14 = 100;
      break;
    case stealth::BrowserProfile::AndroidChromium_Alps:
      params.platform_hints = android_platform();
      params.profile_weights.android_chromium_alps = 100;
      break;
    case stealth::BrowserProfile::Firefox149_Android:
      params.platform_hints = android_platform();
      params.profile_weights.firefox149_android = 100;
      break;
    case stealth::BrowserProfile::Android11_OkHttp_Advisory:
      params.platform_hints = android_platform();
      params.profile_weights.android11_okhttp_advisory = 100;
      break;
    default:
      UNREACHABLE();
  }

  return params;
}

inline string pairing_server_hello_path_for_profile(stealth::BrowserProfile profile) {
  return representative_server_hello_path_for_family(stealth::profile_spec(profile).name).str();
}

inline bool client_hello_advertises_cipher_suite(Slice cipher_suites_bytes, uint16 target_cipher_suite) {
  auto parsed = parse_cipher_suite_vector(cipher_suites_bytes);
  if (parsed.is_error()) {
    return false;
  }
  for (auto cipher_suite : parsed.ok_ref()) {
    if (cipher_suite == target_cipher_suite) {
      return true;
    }
  }
  return false;
}

}  // namespace test
}  // namespace mtproto
}  // namespace td
