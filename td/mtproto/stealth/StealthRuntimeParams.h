// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/StealthConfig.h"

#include "td/utils/Status.h"

namespace td {
namespace mtproto {
namespace stealth {

enum class RuntimeActivePolicy : uint8 {
  Unknown = 0,
  RuEgress = 1,
  NonRuEgress = 2,
};

struct RuntimeRoutePolicyEntry final {
  EchMode ech_mode{EchMode::Disabled};
  bool allow_quic{false};
};

struct RuntimeRoutePolicy final {
  RuntimeRoutePolicyEntry unknown;
  RuntimeRoutePolicyEntry ru;
  RuntimeRoutePolicyEntry non_ru;
};

struct RuntimeRouteFailurePolicy final {
  uint32 ech_failure_threshold{3};
  double ech_disable_ttl_seconds{300.0};
  bool persist_across_restart{true};
};

struct RuntimeFlowBehaviorPolicy final {
  uint32 max_connects_per_10s_per_destination{6};
  double min_reuse_ratio{0.55};
  uint32 min_conn_lifetime_ms{1500};
  uint32 max_conn_lifetime_ms{180000};
  double max_destination_share{0.70};
  uint32 sticky_domain_rotation_window_sec{900};
  uint32 anti_churn_min_reconnect_interval_ms{300};
};

struct RuntimeDesktopProfileWeights final {
  uint8 chrome133{50};
  uint8 chrome131{20};
  uint8 chrome120{15};
  uint8 firefox148{15};
  uint8 safari26_3{20};
};

struct RuntimeMobileProfileWeights final {
  uint8 ios14{70};
  uint8 android11_okhttp{30};
};

struct RuntimeProfileSelectionPolicy final {
  bool allow_cross_class_rotation{false};
  RuntimeDesktopProfileWeights desktop_darwin{35, 25, 10, 10, 20};
  RuntimeDesktopProfileWeights desktop_non_darwin{50, 20, 15, 15, 0};
  RuntimeMobileProfileWeights mobile;
};

struct StealthRuntimeParams final {
  StealthRuntimeParams() noexcept;

  RuntimeActivePolicy active_policy{RuntimeActivePolicy::Unknown};
  IptParams ipt_params;
  DrsPolicy drs_policy;
  ProfileWeights profile_weights;
  RuntimePlatformHints platform_hints;
  RuntimeFlowBehaviorPolicy flow_behavior;
  RuntimeProfileSelectionPolicy profile_selection;
  RuntimeRoutePolicy route_policy;
  RuntimeRouteFailurePolicy route_failure;
  size_t bulk_threshold_bytes{8192};
};

StealthRuntimeParams default_runtime_stealth_params() noexcept;
Status validate_runtime_stealth_params(const StealthRuntimeParams &params) noexcept;
StealthRuntimeParams get_runtime_stealth_params_snapshot() noexcept;
Status set_runtime_stealth_params(const StealthRuntimeParams &params) noexcept;
Status set_runtime_stealth_params_for_tests(const StealthRuntimeParams &params) noexcept;
void reset_runtime_stealth_params_for_tests() noexcept;

}  // namespace stealth
}  // namespace mtproto
}  // namespace td