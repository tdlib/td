// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/mtproto/stealth/StealthConfig.h"

#include <cmath>
#include <memory>

namespace td {
namespace mtproto {
namespace stealth {
namespace {

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
  weights.firefox148 = desktop_weights->firefox148;
  weights.safari26_3 = desktop_weights->safari26_3;
  weights.ios14 = policy.mobile.ios14;
  weights.android11_okhttp = policy.mobile.android11_okhttp;
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
  if (flow_behavior.max_connects_per_10s_per_destination < 1 ||
      flow_behavior.max_connects_per_10s_per_destination > 30) {
    return Status::Error("flow_behavior.max_connects_per_10s_per_destination must be within [1, 30]");
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

  const uint32 mobile_total = policy.mobile.ios14 + policy.mobile.android11_okhttp;
  if (mobile_total != 100) {
    return Status::Error("profile_weights.mobile must sum to 100");
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

Status validate_route_entry(Slice name, const RuntimeRoutePolicyEntry &entry, bool must_disable_ech) {
  if (entry.allow_quic) {
    return Status::Error(name.str() + " must keep QUIC disabled");
  }
  if (must_disable_ech && entry.ech_mode != EchMode::Disabled) {
    return Status::Error(name.str() + " must disable ECH");
  }
  if (!must_disable_ech && entry.ech_mode != EchMode::Disabled && entry.ech_mode != EchMode::Rfc9180Outer) {
    return Status::Error(name.str() + " has unsupported ECH mode");
  }
  return Status::OK();
}

Status validate_profile_weights(const ProfileWeights &weights) {
  const uint32 darwin_total =
      weights.chrome133 + weights.chrome131 + weights.chrome120 + weights.firefox148 + weights.safari26_3;
  const uint32 non_darwin_total = weights.chrome133 + weights.chrome131 + weights.chrome120 + weights.firefox148;
  const uint32 mobile_total = weights.ios14 + weights.android11_okhttp;
  if (darwin_total == 0) {
    return Status::Error("desktop darwin profile weights must not be empty");
  }
  if (non_darwin_total == 0) {
    return Status::Error("desktop non-darwin profile weights must not be empty");
  }
  if (mobile_total == 0) {
    return Status::Error("mobile profile weights must not be empty");
  }
  return Status::OK();
}

}  // namespace

StealthRuntimeParams::StealthRuntimeParams() noexcept {
  drs_policy = default_runtime_drs_policy();
  platform_hints = compiled_default_runtime_platform_hints();
  profile_selection = RuntimeProfileSelectionPolicy{};
  profile_weights = effective_profile_weights_for_platform(profile_selection, platform_hints);
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
  TRY_STATUS(validate_platform_hints(params.platform_hints));
  TRY_STATUS(validate_flow_behavior(params.flow_behavior));
  TRY_STATUS(validate_runtime_profile_selection_policy(params.profile_selection));
  TRY_STATUS(validate_profile_weights(params.profile_weights));
  TRY_STATUS(validate_route_entry("route_policy.unknown", params.route_policy.unknown, true));
  TRY_STATUS(validate_route_entry("route_policy.ru", params.route_policy.ru, true));
  TRY_STATUS(validate_route_entry("route_policy.non_ru", params.route_policy.non_ru, false));

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
  if (params.bulk_threshold_bytes < 512 || params.bulk_threshold_bytes > (static_cast<size_t>(1) << 20)) {
    return Status::Error("bulk_threshold_bytes is out of allowed bounds");
  }
  return Status::OK();
}

StealthRuntimeParams get_runtime_stealth_params_snapshot() noexcept {
  auto params = std::atomic_load(&runtime_params_storage());
  CHECK(params != nullptr);
  return *params;
}

Status set_runtime_stealth_params(const StealthRuntimeParams &params) noexcept {
  TRY_STATUS(validate_runtime_stealth_params(params));
  std::atomic_store(&runtime_params_storage(), std::make_shared<const StealthRuntimeParams>(params));
  return Status::OK();
}

Status set_runtime_stealth_params_for_tests(const StealthRuntimeParams &params) noexcept {
  return set_runtime_stealth_params(params);
}

void reset_runtime_stealth_params_for_tests() noexcept {
  std::atomic_store(&runtime_params_storage(), make_default_runtime_params());
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td