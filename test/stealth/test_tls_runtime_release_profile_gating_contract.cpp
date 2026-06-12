// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::get_runtime_profile_selection_counters;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::ProfileWeights;
using td::mtproto::stealth::reset_runtime_profile_selection_counters_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthRuntimeParams;
using td::mtproto::stealth::TransportConfidence;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_profile_selection_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
    reset_runtime_profile_selection_counters_for_tests();
  }
};

RuntimePlatformHints make_darwin_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Darwin;
  return platform;
}

RuntimePlatformHints make_android_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  platform.mobile_os = MobileOs::Android;
  return platform;
}

TEST(TlsRuntimeReleaseProfileGatingContract, ReleaseModeBlocksAdvisoryDarwinProfileSelection) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.platform_hints = make_darwin_platform();
  params.release_mode_profile_gating = true;
  params.profile_weights = ProfileWeights{};
  params.transport_confidence = TransportConfidence::Strong;
  params.profile_weights.chrome133 = 0;
  params.profile_weights.chrome131 = 0;
  params.profile_weights.chrome120 = 0;
  params.profile_weights.firefox148 = 0;
  params.profile_weights.chromium_macos_no_alps = 1;
  params.profile_weights.chromium_macos_4469 = 1;
  params.profile_weights.chromium_macos_44cd = 1;
  params.profile_weights.firefox149_macos26_3 = 1;
  params.profile_weights.safari26_3 = 100;
  params.profile_weights.ios14 = 70;
  params.profile_weights.firefox149_android = 0;
  params.profile_weights.android11_okhttp_advisory = 30;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto darwin = make_darwin_platform();
  for (td::uint32 bucket = 25000; bucket < 25256; bucket++) {
    auto unix_time = static_cast<td::int32>(bucket * 86400 + 1234);
    for (td::uint32 idx = 0; idx < 64; idx++) {
      td::string domain = "darwin-release-" + td::to_string(bucket) + "-" + td::to_string(idx) + ".example";
      ASSERT_TRUE(pick_runtime_profile(domain, unix_time, darwin) != BrowserProfile::Safari26_3);
    }
  }

  auto counters = get_runtime_profile_selection_counters();
  ASSERT_TRUE(counters.advisory_blocked_total > 0);
}

TEST(TlsRuntimeReleaseProfileGatingContract, AdvisorySelectionAttemptsAreCountedWhenReleaseModeBlocksThem) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.platform_hints = make_darwin_platform();
  params.profile_weights = ProfileWeights{};
  params.transport_confidence = TransportConfidence::Strong;
  params.profile_weights.chrome133 = 0;
  params.profile_weights.chrome131 = 0;
  params.profile_weights.chrome120 = 0;
  params.profile_weights.firefox148 = 0;
  params.profile_weights.chromium_macos_no_alps = 1;
  params.profile_weights.chromium_macos_4469 = 1;
  params.profile_weights.chromium_macos_44cd = 1;
  params.profile_weights.firefox149_macos26_3 = 1;
  params.profile_weights.safari26_3 = 100;
  params.profile_weights.ios14 = 70;
  params.profile_weights.firefox149_android = 0;
  params.profile_weights.android11_okhttp_advisory = 30;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto darwin = make_darwin_platform();
  bool found_advisory_candidate = false;
  td::string candidate_domain;
  td::int32 candidate_unix_time = 0;
  for (td::uint32 idx = 0; idx < 8192 && !found_advisory_candidate; idx++) {
    auto unix_time = 1712345678 + static_cast<td::int32>(idx * 86400);
    td::string domain = "darwin-advisory-candidate-" + td::to_string(idx) + ".example";
    if (pick_runtime_profile(domain, unix_time, darwin) == BrowserProfile::Safari26_3) {
      found_advisory_candidate = true;
      candidate_domain = domain;
      candidate_unix_time = unix_time;
    }
  }
  ASSERT_TRUE(found_advisory_candidate);

  params.release_mode_profile_gating = true;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto picked = pick_runtime_profile(candidate_domain, candidate_unix_time, darwin);
  ASSERT_TRUE(picked != BrowserProfile::Safari26_3);

  auto counters = get_runtime_profile_selection_counters();
  ASSERT_TRUE(counters.advisory_blocked_total >= 1);
}

TEST(TlsRuntimeReleaseProfileGatingContract, ReleaseFallbackNeverEscapesRuntimePlatformSet) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.profile_weights = ProfileWeights{};
  params.platform_hints = make_android_platform();
  params.transport_confidence = TransportConfidence::Strong;
  params.profile_weights.ios14 = 0;
  params.profile_weights.android_chromium_alps = 1;
  params.profile_weights.firefox149_android = 0;
  params.profile_weights.android11_okhttp_advisory = 100;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto android = make_android_platform();
  bool found_advisory_candidate = false;
  td::string candidate_domain;
  td::int32 candidate_unix_time = 0;
  for (td::uint32 idx = 0; idx < 8192 && !found_advisory_candidate; idx++) {
    auto unix_time = 1713345678 + static_cast<td::int32>(idx * 86400);
    td::string domain = "android-advisory-candidate-" + td::to_string(idx) + ".example";
    if (pick_runtime_profile(domain, unix_time, android) == BrowserProfile::Android11_OkHttp_Advisory) {
      found_advisory_candidate = true;
      candidate_domain = domain;
      candidate_unix_time = unix_time;
    }
  }
  ASSERT_TRUE(found_advisory_candidate);

  params.release_mode_profile_gating = true;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto picked = pick_runtime_profile(candidate_domain, candidate_unix_time, android);
  ASSERT_TRUE(picked == BrowserProfile::AndroidChromium_Alps);
  ASSERT_TRUE(picked != BrowserProfile::Chrome133);

  auto counters = get_runtime_profile_selection_counters();
  ASSERT_TRUE(counters.advisory_blocked_total >= 1);
}

}  // namespace
