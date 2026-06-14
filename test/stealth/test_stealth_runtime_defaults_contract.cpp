// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace stealth_runtime_defaults_contract_test {

using td::mtproto::stealth::default_runtime_platform_hints;
using td::mtproto::stealth::default_runtime_stealth_params;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::ProfileWeights;
using td::mtproto::stealth::RuntimeActivePolicy;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::TransportConfidence;
using td::mtproto::stealth::validate_runtime_stealth_params;

void assert_bin(const td::mtproto::stealth::RecordSizeBin &bin, td::int32 lo, td::int32 hi, td::uint32 weight) {
  ASSERT_EQ(lo, bin.lo);
  ASSERT_EQ(hi, bin.hi);
  ASSERT_EQ(weight, bin.weight);
}

ProfileWeights expected_profile_weights_for_platform(const RuntimePlatformHints &platform) {
  ProfileWeights weights;

  if (platform.device_class == DeviceClass::Desktop && platform.desktop_os == DesktopOs::Darwin) {
    weights.chrome133 = 35;
    weights.chrome131 = 25;
    weights.chrome120 = 10;
    weights.firefox148 = 10;
    weights.safari26_3 = 20;
  } else {
    weights.chrome133 = 50;
    weights.chrome131 = 20;
    weights.chrome120 = 15;
    weights.firefox148 = 15;
    weights.safari26_3 = 0;
  }

  // Platform-specific explicit lanes are always bridged from the desktop ratios.
  // macOS Firefox is bridged from the darwin firefox ratio (10) on every platform.
  // Dedicated macOS Chromium cohorts are bridged from the darwin Chromium shares.
  // Mobile shares are bridged into explicit verified/advisory lanes without changing
  // the plan-style mobile policy schema: iOS(70) -> {Chromium 10, AppleIosTls 10,
  // IOS14 50} and Android(30) -> {AndroidChromium_Alps 20, Firefox149_Android 5,
  // Android11_OkHttp_Advisory 5}.
  weights.chrome147_windows = 50;
  weights.chromium_macos_no_alps = 10;
  weights.chromium_macos_4469 = 25;
  weights.chromium_macos_44cd = 35;
  weights.firefox149_macos26_3 = 10;
  weights.firefox149_windows = 15;
  weights.chrome147_ios_chromium = 10;
  weights.apple_ios_tls = 10;
  weights.ios14 = 50;
  weights.firefox149_android = 5;
  weights.android_chromium_alps = 20;
  weights.android11_okhttp_advisory = 5;
  return weights;
}

void assert_profile_weights_eq(const ProfileWeights &lhs, const ProfileWeights &rhs) {
  ASSERT_EQ(lhs.chrome133, rhs.chrome133);
  ASSERT_EQ(lhs.chrome131, rhs.chrome131);
  ASSERT_EQ(lhs.chrome120, rhs.chrome120);
  ASSERT_EQ(lhs.chrome147_windows, rhs.chrome147_windows);
  ASSERT_EQ(lhs.chromium_macos_no_alps, rhs.chromium_macos_no_alps);
  ASSERT_EQ(lhs.chromium_macos_4469, rhs.chromium_macos_4469);
  ASSERT_EQ(lhs.chromium_macos_44cd, rhs.chromium_macos_44cd);
  ASSERT_EQ(lhs.chrome147_ios_chromium, rhs.chrome147_ios_chromium);
  ASSERT_EQ(lhs.firefox148, rhs.firefox148);
  ASSERT_EQ(lhs.firefox149_android, rhs.firefox149_android);
  ASSERT_EQ(lhs.firefox149_macos26_3, rhs.firefox149_macos26_3);
  ASSERT_EQ(lhs.firefox149_windows, rhs.firefox149_windows);
  ASSERT_EQ(lhs.safari26_3, rhs.safari26_3);
  ASSERT_EQ(lhs.ios14, rhs.ios14);
  ASSERT_EQ(lhs.apple_ios_tls, rhs.apple_ios_tls);
  ASSERT_EQ(lhs.android_chromium_alps, rhs.android_chromium_alps);
  ASSERT_EQ(lhs.android11_okhttp_advisory, rhs.android11_okhttp_advisory);
}

TEST(StealthRuntimeDefaultsContract, DefaultRuntimeParamsValidateAndExposeFailClosedRouteDefaults) {
  auto params = default_runtime_stealth_params();

  ASSERT_TRUE(validate_runtime_stealth_params(params).is_ok());
  ASSERT_TRUE(params.active_policy == RuntimeActivePolicy::Unknown);
  ASSERT_TRUE(params.transport_confidence == TransportConfidence::Unknown);
  ASSERT_FALSE(params.release_mode_profile_gating);
  ASSERT_FALSE(params.require_per_install_selection_salt);

  ASSERT_TRUE(params.route_policy.unknown.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(params.route_policy.ru.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(params.route_policy.non_ru.ech_mode == EchMode::Rfc9180Outer);

  ASSERT_EQ(3u, params.route_failure.ech_failure_threshold);
  ASSERT_EQ(300.0, params.route_failure.ech_disable_ttl_seconds);
  ASSERT_TRUE(params.route_failure.persist_across_restart);
  ASSERT_EQ(static_cast<size_t>(8192), params.bulk_threshold_bytes);
}

TEST(StealthRuntimeDefaultsContract, DefaultRuntimeParamsPublishExpectedFallbackDrsPolicy) {
  auto params = default_runtime_stealth_params();

  ASSERT_EQ(2u, params.drs_policy.slow_start.bins.size());
  assert_bin(params.drs_policy.slow_start.bins[0], 1200, 1460, 1);
  assert_bin(params.drs_policy.slow_start.bins[1], 1461, 1700, 1);
  ASSERT_EQ(4, params.drs_policy.slow_start.max_repeat_run);
  ASSERT_EQ(24, params.drs_policy.slow_start.local_jitter);

  ASSERT_EQ(2u, params.drs_policy.congestion_open.bins.size());
  assert_bin(params.drs_policy.congestion_open.bins[0], 1400, 1900, 1);
  assert_bin(params.drs_policy.congestion_open.bins[1], 1901, 2600, 2);
  ASSERT_EQ(4, params.drs_policy.congestion_open.max_repeat_run);
  ASSERT_EQ(24, params.drs_policy.congestion_open.local_jitter);

  ASSERT_EQ(3u, params.drs_policy.steady_state.bins.size());
  assert_bin(params.drs_policy.steady_state.bins[0], 2400, 4096, 2);
  assert_bin(params.drs_policy.steady_state.bins[1], 4097, 8192, 2);
  assert_bin(params.drs_policy.steady_state.bins[2], 8193, 12288, 1);
  ASSERT_EQ(4, params.drs_policy.steady_state.max_repeat_run);
  ASSERT_EQ(24, params.drs_policy.steady_state.local_jitter);
}

TEST(StealthRuntimeDefaultsContract, DefaultRuntimeParamsDeriveExpectedProfileWeightsForCurrentPlatform) {
  auto params = default_runtime_stealth_params();
  auto expected_platform = default_runtime_platform_hints();

  ASSERT_TRUE(params.platform_hints.device_class == expected_platform.device_class);
  ASSERT_TRUE(params.platform_hints.mobile_os == expected_platform.mobile_os);
  ASSERT_TRUE(params.platform_hints.desktop_os == expected_platform.desktop_os);

  auto expected_weights = expected_profile_weights_for_platform(expected_platform);
  assert_profile_weights_eq(expected_weights, params.profile_weights);

  ASSERT_FALSE(params.profile_selection.allow_cross_class_rotation);
  ASSERT_EQ(35, params.profile_selection.desktop_darwin.chrome133);
  ASSERT_EQ(25, params.profile_selection.desktop_darwin.chrome131);
  ASSERT_EQ(10, params.profile_selection.desktop_darwin.chrome120);
  ASSERT_EQ(10, params.profile_selection.desktop_darwin.firefox148);
  ASSERT_EQ(20, params.profile_selection.desktop_darwin.safari26_3);
  ASSERT_EQ(50, params.profile_selection.desktop_non_darwin.chrome133);
  ASSERT_EQ(20, params.profile_selection.desktop_non_darwin.chrome131);
  ASSERT_EQ(15, params.profile_selection.desktop_non_darwin.chrome120);
  ASSERT_EQ(15, params.profile_selection.desktop_non_darwin.firefox148);
  ASSERT_EQ(0, params.profile_selection.desktop_non_darwin.safari26_3);
  ASSERT_EQ(70, params.profile_selection.mobile.ios14);
  ASSERT_EQ(30, params.profile_selection.mobile.android11_okhttp_advisory);
}

}  // namespace stealth_runtime_defaults_contract_test
