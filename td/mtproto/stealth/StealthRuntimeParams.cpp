// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/mtproto/stealth/StealthConfig.h"

#include <cmath>
#include <memory>
#include <mutex>

namespace td {
namespace mtproto {
namespace stealth {
namespace stealth_runtime_params_internal {

RuntimePlatformHints compiled_default_runtime_platform_hints() {
  RuntimePlatformHints hints;
#if TD_ANDROID
  hints.device_class = DeviceClass::Mobile;
  hints.mobile_os = MobileOs::Android;
  hints.desktop_os = DesktopOs::Unknown;
#elif TD_DARWIN_IOS || TD_DARWIN_TV_OS || TD_DARWIN_VISION_OS || TD_DARWIN_WATCH_OS
  hints.device_class = DeviceClass::Mobile;
  hints.mobile_os = MobileOs::IOS;
  hints.desktop_os = DesktopOs::Unknown;
#else
  hints.device_class = DeviceClass::Desktop;
#if TD_DARWIN
  hints.desktop_os = DesktopOs::Darwin;
#elif TD_WINDOWS
  hints.desktop_os = DesktopOs::Windows;
#else
  hints.desktop_os = DesktopOs::Linux;
#endif
#endif
  return hints;
}

DrsPolicy default_runtime_drs_policy() {
  DrsPolicy policy;
  policy.slow_start.bins = {{1200, 1460, 1}, {1461, 1700, 1}};
  policy.slow_start.max_repeat_run = 4;
  policy.slow_start.local_jitter = 24;
  policy.congestion_open.bins = {{1400, 1900, 1}, {1901, 2600, 2}};
  policy.congestion_open.max_repeat_run = 4;
  policy.congestion_open.local_jitter = 24;
  policy.steady_state.bins = {{2400, 4096, 2}, {4097, 8192, 2}, {8193, 12288, 1}};
  policy.steady_state.max_repeat_run = 4;
  policy.steady_state.local_jitter = 24;
  return policy;
}

ProfileWeights effective_profile_weights_for_platform(const RuntimeProfileSelectionPolicy &policy,
                                                      const RuntimePlatformHints &platform) {
  ProfileWeights weights;
  const auto *desktop_weights = &policy.desktop_non_darwin;
  if (platform.device_class == DeviceClass::Desktop && platform.desktop_os == DesktopOs::Darwin) {
    desktop_weights = &policy.desktop_darwin;
  }

  weights.chrome133 = desktop_weights->chrome133;
  weights.chrome131 = desktop_weights->chrome131;
  weights.chrome120 = desktop_weights->chrome120;
  // Keep platform-specific lanes populated from the policy source of truth.
  // Windows lanes inherit desktop_non_darwin desktop-family ratios.
  weights.chrome147_windows = policy.desktop_non_darwin.chrome133;
  weights.firefox149_windows = policy.desktop_non_darwin.firefox148;
  weights.chromium_macos_no_alps = policy.desktop_darwin.chrome120;
  weights.chromium_macos_4469 = policy.desktop_darwin.chrome131;
  weights.chromium_macos_44cd = policy.desktop_darwin.chrome133;
  // Carve slices of the iOS share for the verified iOS Chromium lane and the
  // verified Apple iOS TLS lane instead of pinning them to 0 (which left iOS with
  // only the advisory utls IOS14 lane); the remainder stays with IOS14. Keeps the
  // ios14+android policy schema unchanged.
  auto ios_chromium_weight = static_cast<uint8>(policy.mobile.ios14 / kIosChromiumShareDivisor);
  auto apple_ios_tls_weight = static_cast<uint8>(policy.mobile.ios14 / kAppleIosTlsShareDivisor);
  weights.chrome147_ios_chromium = ios_chromium_weight;
  weights.apple_ios_tls = apple_ios_tls_weight;
  weights.firefox148 = desktop_weights->firefox148;
  // macOS Firefox (Firefox149_MacOS26_3) is the Firefox lane on Darwin desktop;
  // bridge it from the darwin policy's firefox ratio so it can be tuned
  // independently of the Linux firefox148 lane. Populated on every platform
  // (like the windows lanes) but only selected where it is an allowed profile.
  weights.firefox149_macos26_3 = policy.desktop_darwin.firefox148;
  weights.safari26_3 = desktop_weights->safari26_3;
  weights.ios14 = static_cast<uint8>(policy.mobile.ios14 - ios_chromium_weight - apple_ios_tls_weight);
  auto android_chromium_alps_weight = static_cast<uint8>(
      policy.mobile.android11_okhttp_advisory * kAndroidChromiumVerifiedShareNumerator /
      kAndroidChromiumVerifiedShareDenominator);
  auto android_residual_weight =
      static_cast<uint8>(policy.mobile.android11_okhttp_advisory - android_chromium_alps_weight);
  auto android_firefox_weight = static_cast<uint8>(android_residual_weight / kAndroidFirefoxResidualShareDivisor);
  weights.firefox149_android = android_firefox_weight;
  weights.android_chromium_alps = android_chromium_alps_weight;
  weights.android11_okhttp_advisory =
      static_cast<uint8>(android_residual_weight - android_firefox_weight);
  return weights;
}

Status validate_platform_hints(const RuntimePlatformHints &platform) {
  if (platform.device_class == DeviceClass::Desktop) {
    if (platform.mobile_os != MobileOs::None) {
      return Status::Error("platform_hints.desktop device_class must set mobile_os to none");
    }
    if (platform.desktop_os == DesktopOs::Unknown) {
      return Status::Error("platform_hints.desktop device_class must set a concrete desktop_os");
    }
    return Status::OK();
  }

  if (platform.mobile_os == MobileOs::None) {
    return Status::Error("platform_hints.mobile device_class must set a concrete mobile_os");
  }
  if (platform.desktop_os != DesktopOs::Unknown) {
    return Status::Error("platform_hints.mobile device_class must keep desktop_os unknown");
  }
  return Status::OK();
}

Status validate_flow_behavior(const RuntimeFlowBehaviorPolicy &flow_behavior) {
  if (flow_behavior.max_connects_per_10s_per_destination > 30) {
    return Status::Error("flow_behavior.max_connects_per_10s_per_destination must be within [0, 30]");
  }
  if (!std::isfinite(flow_behavior.min_reuse_ratio) || flow_behavior.min_reuse_ratio < 0.0 ||
      flow_behavior.min_reuse_ratio > 1.0) {
    return Status::Error("flow_behavior.min_reuse_ratio must be within [0, 1]");
  }
  if (flow_behavior.min_conn_lifetime_ms < 200 || flow_behavior.min_conn_lifetime_ms > 600000) {
    return Status::Error("flow_behavior.min_conn_lifetime_ms must be within [200, 600000]");
  }
  if (flow_behavior.max_conn_lifetime_ms < flow_behavior.min_conn_lifetime_ms ||
      flow_behavior.max_conn_lifetime_ms > 3600000) {
    return Status::Error("flow_behavior.max_conn_lifetime_ms must be within [min_conn_lifetime_ms, 3600000]");
  }
  if (!std::isfinite(flow_behavior.max_destination_share) || flow_behavior.max_destination_share <= 0.0 ||
      flow_behavior.max_destination_share > 1.0) {
    return Status::Error("flow_behavior.max_destination_share must be within (0, 1]");
  }
  if (flow_behavior.sticky_domain_rotation_window_sec < 60 || flow_behavior.sticky_domain_rotation_window_sec > 86400) {
    return Status::Error("flow_behavior.sticky_domain_rotation_window_sec must be within [60, 86400]");
  }
  if (flow_behavior.anti_churn_min_reconnect_interval_ms < 50 ||
      flow_behavior.anti_churn_min_reconnect_interval_ms > 60000) {
    return Status::Error("flow_behavior.anti_churn_min_reconnect_interval_ms must be within [50, 60000]");
  }
  return Status::OK();
}

Status validate_runtime_profile_selection_policy(const RuntimeProfileSelectionPolicy &policy) {
  if (policy.allow_cross_class_rotation) {
    return Status::Error("profile_weights.allow_cross_class_rotation must stay disabled");
  }

  const uint32 darwin_total = policy.desktop_darwin.chrome133 + policy.desktop_darwin.chrome131 +
                              policy.desktop_darwin.chrome120 + policy.desktop_darwin.firefox148 +
                              policy.desktop_darwin.safari26_3;
  if (darwin_total != 100) {
    return Status::Error("profile_weights.desktop_darwin must sum to 100");
  }

  const uint32 non_darwin_total = policy.desktop_non_darwin.chrome133 + policy.desktop_non_darwin.chrome131 +
                                  policy.desktop_non_darwin.chrome120 + policy.desktop_non_darwin.firefox148 +
                                  policy.desktop_non_darwin.safari26_3;
  if (non_darwin_total != 100) {
    return Status::Error("profile_weights.desktop_non_darwin must sum to 100");
  }
  if (policy.desktop_non_darwin.safari26_3 != 0) {
    return Status::Error("profile_weights.desktop_non_darwin.safari26_3 must be 0");
  }

  const uint32 mobile_total = policy.mobile.ios14 + policy.mobile.android11_okhttp_advisory;
  if (mobile_total != 100) {
    return Status::Error("profile_weights.mobile must sum to 100");
  }
  // The iOS share is carved into the verified iOS Chromium and Apple iOS TLS lanes
  // by integer division (ios14 / 7 each). For ios14 in [1, 6] both carves truncate
  // to 0, silently collapsing iOS back onto the advisory IOS14 lane and re-opening
  // the release-grade gap. Require either no iOS share or one large enough to keep
  // both verified lanes reachable. (Explicit flat profile_weights configs set
  // apple_ios_tls directly and are not affected by this policy-path floor.)
  if (policy.mobile.ios14 != 0 && policy.mobile.ios14 < kIosChromiumShareDivisor) {
    return Status::Error("profile_weights.mobile.ios14 must be 0 or at least 7 so the verified iOS lanes stay reachable");
  }
  return Status::OK();
}

std::shared_ptr<const StealthRuntimeParams> make_default_runtime_params() {
  return std::make_shared<const StealthRuntimeParams>(default_runtime_stealth_params());
}

std::shared_ptr<const StealthRuntimeParams> &runtime_params_storage() {
  static auto params = make_default_runtime_params();
  return params;
}

std::mutex &runtime_params_storage_mutex() {
  static std::mutex mu;
  return mu;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
Status
error_from_owned_message(std::string message) {
  return Status::Error(Slice(message.data(), message.size()));
}

Status validate_route_entry(Slice name, const RuntimeRoutePolicyEntry &entry, bool must_disable_ech) {
  if (entry.allow_quic) {
    return error_from_owned_message(name.str() + " must keep QUIC disabled");
  }
  if (must_disable_ech && entry.ech_mode != EchMode::Disabled) {
    return error_from_owned_message(name.str() + " must disable ECH");
  }
  if (!must_disable_ech && entry.ech_mode != EchMode::Disabled && entry.ech_mode != EchMode::Rfc9180Outer) {
    return error_from_owned_message(name.str() + " has unsupported ECH mode");
  }
  return Status::OK();
}

Status validate_profile_weights(const ProfileWeights &weights) {
  const uint32 total = weights.chrome133 + weights.chrome131 + weights.chrome120 + weights.chrome147_windows +
                       weights.chromium_macos_no_alps + weights.chromium_macos_4469 + weights.chromium_macos_44cd +
                       weights.chrome147_ios_chromium + weights.firefox148 + weights.firefox149_android +
                       weights.firefox149_macos26_3 + weights.firefox149_windows + weights.safari26_3 + weights.ios14 +
                       weights.apple_ios_tls + weights.android_chromium_alps +
                       weights.android11_okhttp_advisory;
  if (total == 0) {
    return Status::Error("profile_weights must not be empty");
  }
  return Status::OK();
}

Status validate_allowed_profile_weights_for_platform(const ProfileWeights &weights,
                                                     const RuntimePlatformHints &platform) {
  uint32 allowed_total = 0;
  if (platform.device_class == DeviceClass::Mobile) {
    switch (platform.mobile_os) {
      case MobileOs::IOS:
        allowed_total = weights.ios14 + weights.chrome147_ios_chromium + weights.apple_ios_tls;
        break;
      case MobileOs::Android:
        allowed_total = weights.android_chromium_alps + weights.firefox149_android + weights.android11_okhttp_advisory;
        break;
      case MobileOs::None:
      default:
        allowed_total = weights.ios14 + weights.chrome147_ios_chromium + weights.apple_ios_tls +
                        weights.android_chromium_alps + weights.firefox149_android + weights.android11_okhttp_advisory;
        break;
    }
  } else if (platform.desktop_os == DesktopOs::Darwin) {
    allowed_total = weights.chromium_macos_no_alps + weights.chromium_macos_4469 + weights.chromium_macos_44cd +
                    weights.firefox149_macos26_3 + weights.safari26_3;
  } else if (platform.desktop_os == DesktopOs::Windows) {
    allowed_total = weights.chrome147_windows + weights.firefox149_windows;
  } else {
    allowed_total = weights.chrome133 + weights.chrome131 + weights.chrome120 + weights.firefox148;
  }

  if (allowed_total == 0) {
    return Status::Error("platform_hints must keep at least one allowed profile weight enabled");
  }
  return Status::OK();
}

uint8 profile_weight_for_runtime_validation(const ProfileWeights &weights, BrowserProfile profile) {
  switch (profile) {
    case BrowserProfile::Chrome133:
      return weights.chrome133;
    case BrowserProfile::Chrome131:
      return weights.chrome131;
    case BrowserProfile::Chrome120:
      return weights.chrome120;
    case BrowserProfile::Chrome147_Windows:
      return weights.chrome147_windows;
    case BrowserProfile::ChromiumMacOS_NoAlps:
      return weights.chromium_macos_no_alps;
    case BrowserProfile::ChromiumMacOS_4469:
      return weights.chromium_macos_4469;
    case BrowserProfile::ChromiumMacOS_44CD:
      return weights.chromium_macos_44cd;
    case BrowserProfile::Chrome147_IOSChromium:
      return weights.chrome147_ios_chromium;
    case BrowserProfile::Firefox148:
      return weights.firefox148;
    case BrowserProfile::Firefox149_Android:
      return weights.firefox149_android;
    case BrowserProfile::Firefox149_MacOS26_3:
      return weights.firefox149_macos26_3;
    case BrowserProfile::Firefox149_Windows:
      return weights.firefox149_windows;
    case BrowserProfile::Safari26_3:
      return weights.safari26_3;
    case BrowserProfile::IOS14:
      return weights.ios14;
    case BrowserProfile::AppleIosTls:
      return weights.apple_ios_tls;
    case BrowserProfile::AndroidChromium_Alps:
      return weights.android_chromium_alps;
    case BrowserProfile::Android11_OkHttp_Advisory:
      return weights.android11_okhttp_advisory;
    default:
      UNREACHABLE();
      return 0;
  }
}

Status validate_transport_confidence_profile_coverage(const StealthRuntimeParams &params) {
  if (params.transport_confidence != TransportConfidence::Unknown) {
    return Status::OK();
  }

  uint32 tls_only_allowed_total = 0;
  auto allowed_profiles = allowed_profiles_for_platform(params.platform_hints);
  for (auto profile : allowed_profiles) {
    if (profile_fixture_metadata(profile).transport_claim_level != TransportClaimLevel::TlsOnly) {
      continue;
    }
    tls_only_allowed_total += profile_weight_for_runtime_validation(params.profile_weights, profile);
  }

  if (tls_only_allowed_total == 0) {
    return Status::Error(
        "transport_confidence.unknown requires at least one tls-only profile weight for platform_hints");
  }
  return Status::OK();
}

Status validate_release_mode_profile_gating(const StealthRuntimeParams &params) {
  if (!params.release_mode_profile_gating) {
    return Status::OK();
  }

  uint32 release_weight_total = 0;
  auto allowed_profiles = allowed_profiles_for_platform(params.platform_hints);
  for (auto profile : allowed_profiles) {
    if (!profile_fixture_metadata(profile).release_gating) {
      continue;
    }
    if (params.transport_confidence == TransportConfidence::Unknown &&
        profile_fixture_metadata(profile).transport_claim_level != TransportClaimLevel::TlsOnly) {
      continue;
    }
    release_weight_total += profile_weight_for_runtime_validation(params.profile_weights, profile);
  }
  if (release_weight_total == 0) {
    return Status::Error(
        "release_mode_profile_gating requires at least one release_gating profile weight for platform_hints");
  }
  return Status::OK();
}

}  // namespace stealth_runtime_params_internal

StealthRuntimeParams::StealthRuntimeParams() noexcept {
  drs_policy = stealth_runtime_params_internal::default_runtime_drs_policy();
  platform_hints = stealth_runtime_params_internal::compiled_default_runtime_platform_hints();
  profile_selection = RuntimeProfileSelectionPolicy{};
  profile_weights =
      stealth_runtime_params_internal::effective_profile_weights_for_platform(profile_selection, platform_hints);
  route_policy.unknown.ech_mode = EchMode::Disabled;
  route_policy.ru.ech_mode = EchMode::Disabled;
  route_policy.non_ru.ech_mode = EchMode::Rfc9180Outer;
}

StealthRuntimeParams default_runtime_stealth_params() noexcept {
  return StealthRuntimeParams{};
}

Status validate_runtime_stealth_params(const StealthRuntimeParams &params) noexcept {
  TRY_STATUS(validate_ipt_params(params.ipt_params));
  TRY_STATUS(validate_drs_policy(params.drs_policy));
  TRY_STATUS(stealth_runtime_params_internal::validate_platform_hints(params.platform_hints));
  TRY_STATUS(stealth_runtime_params_internal::validate_flow_behavior(params.flow_behavior));
  TRY_STATUS(stealth_runtime_params_internal::validate_runtime_profile_selection_policy(params.profile_selection));
  TRY_STATUS(stealth_runtime_params_internal::validate_profile_weights(params.profile_weights));
  TRY_STATUS(stealth_runtime_params_internal::validate_allowed_profile_weights_for_platform(params.profile_weights,
                                                                                            params.platform_hints));
  TRY_STATUS(stealth_runtime_params_internal::validate_transport_confidence_profile_coverage(params));
  TRY_STATUS(stealth_runtime_params_internal::validate_release_mode_profile_gating(params));
  TRY_STATUS(
      stealth_runtime_params_internal::validate_route_entry("route_policy.unknown", params.route_policy.unknown, true));
  TRY_STATUS(stealth_runtime_params_internal::validate_route_entry("route_policy.ru", params.route_policy.ru, true));
  TRY_STATUS(
      stealth_runtime_params_internal::validate_route_entry("route_policy.non_ru", params.route_policy.non_ru, false));

  if (params.route_failure.ech_failure_threshold < 1 || params.route_failure.ech_failure_threshold > 10) {
    return Status::Error("route_failure.ech_failure_threshold must be within [1, 10]");
  }
  if (!std::isfinite(params.route_failure.ech_disable_ttl_seconds) ||
      params.route_failure.ech_disable_ttl_seconds < 60.0 || params.route_failure.ech_disable_ttl_seconds > 86400.0) {
    return Status::Error("route_failure.ech_disable_ttl_seconds must be within [60, 86400]");
  }
  if (!params.route_failure.persist_across_restart) {
    return Status::Error("route_failure.persist_across_restart must stay enabled");
  }
  if (params.profile_rotation.failure_threshold < 2 || params.profile_rotation.failure_threshold > 8) {
    return Status::Error("profile_rotation.failure_threshold must be within [2, 8]");
  }
  if (!std::isfinite(params.profile_rotation.quarantine_ttl_seconds) ||
      params.profile_rotation.quarantine_ttl_seconds < 30.0 ||
      params.profile_rotation.quarantine_ttl_seconds > 3600.0) {
    return Status::Error("profile_rotation.quarantine_ttl_seconds must be within [30, 3600]");
  }
  if (params.bulk_threshold_bytes < 512 || params.bulk_threshold_bytes > (static_cast<size_t>(1) << 20)) {
    return Status::Error("bulk_threshold_bytes is out of allowed bounds");
  }
  return Status::OK();
}

StealthRuntimeParams get_runtime_stealth_params_snapshot() noexcept {
  auto lock = std::scoped_lock(stealth_runtime_params_internal::runtime_params_storage_mutex());
  auto params = stealth_runtime_params_internal::runtime_params_storage();
  CHECK(params != nullptr);
  return *params;
}

Status set_runtime_stealth_params(const StealthRuntimeParams &params) noexcept {
  TRY_STATUS(validate_runtime_stealth_params(params));
  double previous_ttl_seconds = params.route_failure.ech_disable_ttl_seconds;
  {
    auto lock = std::scoped_lock(stealth_runtime_params_internal::runtime_params_storage_mutex());
    auto current = stealth_runtime_params_internal::runtime_params_storage();
    if (current != nullptr) {
      previous_ttl_seconds = current->route_failure.ech_disable_ttl_seconds;
    }
    stealth_runtime_params_internal::runtime_params_storage() = std::make_shared<const StealthRuntimeParams>(params);
  }

  if (params.route_failure.ech_disable_ttl_seconds < previous_ttl_seconds) {
    reconcile_runtime_ech_failure_ttl(params.route_failure.ech_disable_ttl_seconds);
  }
  return Status::OK();
}

Status set_runtime_stealth_params_for_tests(const StealthRuntimeParams &params) noexcept {
  return set_runtime_stealth_params(params);
}

void reset_runtime_stealth_params_for_tests() noexcept {
  auto lock = std::scoped_lock(stealth_runtime_params_internal::runtime_params_storage_mutex());
  stealth_runtime_params_internal::runtime_params_storage() =
      stealth_runtime_params_internal::make_default_runtime_params();
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
