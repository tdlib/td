// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

namespace tls_runtime_profile_policy_fail_closed {

using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::get_per_install_selection_salt;
using td::mtproto::stealth::get_runtime_stealth_params_snapshot;
using td::mtproto::stealth::ProfileWeights;
using td::mtproto::stealth::reset_per_install_selection_salt_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_per_install_selection_salt;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthRuntimeParams;
using td::mtproto::stealth::TransportConfidence;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }
};

RuntimePlatformHints darwin_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Darwin;
  return platform;
}

RuntimePlatformHints ios_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  platform.mobile_os = td::mtproto::stealth::MobileOs::IOS;
  return platform;
}

RuntimePlatformHints android_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  platform.mobile_os = td::mtproto::stealth::MobileOs::Android;
  return platform;
}

StealthRuntimeParams make_default_params() {
  return StealthRuntimeParams{};
}

ProfileWeights zero_profile_weights() {
  ProfileWeights weights;
  weights.chrome133 = 0;
  weights.chrome131 = 0;
  weights.chrome120 = 0;
  weights.chrome147_windows = 0;
  weights.chromium_macos_no_alps = 0;
  weights.chromium_macos_4469 = 0;
  weights.chromium_macos_44cd = 0;
  weights.chrome147_ios_chromium = 0;
  weights.firefox148 = 0;
  weights.firefox149_android = 0;
  weights.firefox149_macos26_3 = 0;
  weights.firefox149_windows = 0;
  weights.safari26_3 = 0;
  weights.ios14 = 0;
  weights.apple_ios_tls = 0;
  weights.android_chromium_alps = 0;
  weights.android11_okhttp_advisory = 0;
  return weights;
}

void assert_invalid(const StealthRuntimeParams &params, td::Slice expected_message) {
  auto status = set_runtime_stealth_params_for_tests(params);
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ(expected_message.str().c_str(), status.message().c_str());
}

TEST(TlsRuntimeProfilePolicyFailClosed, RejectsEmptyProfileWeights) {
  RuntimeParamsGuard guard;

  auto params = make_default_params();
  params.profile_weights = zero_profile_weights();

  assert_invalid(params, "profile_weights must not be empty");
}

TEST(TlsRuntimeProfilePolicyFailClosed, RejectsCrossClassRotationToggleInRuntimePolicy) {
  RuntimeParamsGuard guard;

  auto params = make_default_params();
  params.profile_selection.allow_cross_class_rotation = true;

  assert_invalid(params, "profile_weights.allow_cross_class_rotation must stay disabled");
}

TEST(TlsRuntimeProfilePolicyFailClosed, RejectsDesktopDarwinWeightsThatDoNotSumToOneHundred) {
  RuntimeParamsGuard guard;

  auto params = make_default_params();
  params.profile_selection.desktop_darwin.chrome133 = 34;

  assert_invalid(params, "profile_weights.desktop_darwin must sum to 100");
}

TEST(TlsRuntimeProfilePolicyFailClosed, RejectsDesktopNonDarwinSafariLaneEnablement) {
  RuntimeParamsGuard guard;

  auto params = make_default_params();
  params.profile_selection.desktop_non_darwin.chrome133 = 49;
  params.profile_selection.desktop_non_darwin.safari26_3 = 1;

  assert_invalid(params, "profile_weights.desktop_non_darwin.safari26_3 must be 0");
}

TEST(TlsRuntimeProfilePolicyFailClosed, RejectsMobileWeightsThatDoNotSumToOneHundred) {
  RuntimeParamsGuard guard;

  auto params = make_default_params();
  params.profile_selection.mobile.ios14 = 69;

  assert_invalid(params, "profile_weights.mobile must sum to 100");
}

TEST(TlsRuntimeProfilePolicyFailClosed, RejectsReleaseModeWhenPlatformHasOnlyAdvisoryWeights) {
  RuntimeParamsGuard guard;

  auto params = make_default_params();
  params.platform_hints = darwin_platform();
  params.transport_confidence = TransportConfidence::Strong;
  params.release_mode_profile_gating = true;
  params.profile_weights = zero_profile_weights();
  params.profile_weights.safari26_3 = 100;

  assert_invalid(params,
                 "release_mode_profile_gating requires at least one release_gating profile weight for platform_hints");
}

TEST(TlsRuntimeProfilePolicyFailClosed, AllowsReleaseModeForCurrentIosCurationAtPartialConfidence) {
  RuntimeParamsGuard guard;

  auto params = make_default_params();
  params.platform_hints = ios_platform();
  params.transport_confidence = TransportConfidence::Partial;
  params.release_mode_profile_gating = true;

  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
}

TEST(TlsRuntimeProfilePolicyFailClosed, AllowsReleaseModeForVerifiedAndroidCurationAtEstablishedConfidence) {
  RuntimeParamsGuard guard;

  auto params = make_default_params();
  params.platform_hints = android_platform();
  params.transport_confidence = TransportConfidence::Strong;
  params.release_mode_profile_gating = true;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
}

TEST(TlsRuntimeProfilePolicyFailClosed, RejectsReleaseModeForAndroidAtUnknownConfidence) {
  RuntimeParamsGuard guard;

  auto params = make_default_params();
  params.platform_hints = android_platform();
  params.transport_confidence = TransportConfidence::Unknown;
  params.release_mode_profile_gating = true;
  assert_invalid(params,
                 "release_mode_profile_gating requires at least one release_gating profile weight for platform_hints");
}

TEST(TlsRuntimeProfilePolicyFailClosed, RejectsRequiredPerInstallSelectionSaltWhenUnset) {
  RuntimeParamsGuard guard;
  reset_per_install_selection_salt_for_tests();
  SCOPE_EXIT {
    reset_per_install_selection_salt_for_tests();
  };

  auto params = make_default_params();
  params.require_per_install_selection_salt = true;
  ASSERT_EQ(static_cast<td::uint64>(0), get_per_install_selection_salt());
  assert_invalid(params, "require_per_install_selection_salt requires a non-zero per-install selection salt");
}

TEST(TlsRuntimeProfilePolicyFailClosed, AllowsRequiredPerInstallSelectionSaltWhenConfigured) {
  RuntimeParamsGuard guard;
  reset_per_install_selection_salt_for_tests();
  SCOPE_EXIT {
    reset_per_install_selection_salt_for_tests();
  };

  set_per_install_selection_salt(0x8F2A5D39C17B4E61ULL);

  auto params = make_default_params();
  params.require_per_install_selection_salt = true;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
}

TEST(TlsRuntimeProfilePolicyFailClosed, InvalidProfilePolicyDoesNotReplaceLastKnownGoodSnapshot) {
  RuntimeParamsGuard guard;

  auto good = make_default_params();
  good.bulk_threshold_bytes = 16384;
  good.profile_selection.desktop_darwin.chrome133 = 35;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(good).is_ok());

  auto invalid = good;
  invalid.profile_selection.desktop_non_darwin.chrome133 = 49;
  invalid.profile_selection.desktop_non_darwin.safari26_3 = 1;
  auto status = set_runtime_stealth_params_for_tests(invalid);
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("profile_weights.desktop_non_darwin.safari26_3 must be 0", status.message().c_str());

  auto snapshot = get_runtime_stealth_params_snapshot();
  ASSERT_EQ(static_cast<size_t>(16384), snapshot.bulk_threshold_bytes);
  ASSERT_EQ(0, snapshot.profile_selection.desktop_non_darwin.safari26_3);
  ASSERT_EQ(50, snapshot.profile_selection.desktop_non_darwin.chrome133);
}

}  // namespace tls_runtime_profile_policy_fail_closed
