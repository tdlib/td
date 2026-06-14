// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
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

enum class TransportConfidence : uint8 {
  Unknown = 0,
  Partial = 1,
  Strong = 2,
};

struct RuntimeRoutePolicyEntry final {
  EchMode ech_mode{EchMode::Disabled};
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
  uint8 android11_okhttp_advisory{30};
};

// Fraction of the iOS share given to the verified browser-capture iOS Chromium
// lane (Chrome147_IOSChromium) when flattening the mobile policy into effective
// weights. The iOS share previously went entirely to the advisory utls IOS14
// lane (chrome147_ios_chromium was pinned to 0), so iOS had no verified lane; the
// carve-out makes it reachable once transport_confidence permits its cross-layer
// claim, while keeping the policy schema (ios14 + android = 100) and its config
// loader backward-compatible. 1/7 of 70 == 10.
constexpr uint8 kIosChromiumShareDivisor = 7;
// Fraction of the iOS share given to the verified browser-capture Apple iOS TLS
// lane (AppleIosTls). Carved alongside kIosChromiumShareDivisor so iOS gains a
// verified TlsOnly + release-gated lane and IOS14 is no longer the only
// Unknown-confidence iOS lane, while the ios14 + android = 100 mobile policy
// schema (and its loader) stays unchanged. 1/7 of 70 == 10.
constexpr uint8 kAppleIosTlsShareDivisor = 7;
constexpr uint8 kAndroidChromiumVerifiedShareNumerator = 2;
constexpr uint8 kAndroidChromiumVerifiedShareDenominator = 3;
constexpr uint8 kAndroidFirefoxResidualShareDivisor = 2;

struct RuntimeProfileSelectionPolicy final {
  bool allow_cross_class_rotation{false};
  RuntimeDesktopProfileWeights desktop_darwin{35, 25, 10, 10, 20};
  RuntimeDesktopProfileWeights desktop_non_darwin{50, 20, 15, 15, 0};
  RuntimeMobileProfileWeights mobile;
};

// Bounded, failure-driven runtime profile rotation. When a specific emitted wire
// variant (destination + BrowserProfile + whether the hello used ECH) is rejected
// `failure_threshold` times, it is quarantined for `quarantine_ttl_seconds` so the
// next connection attempt to that destination can avoid that one fingerprint and
// try another already-allowed wire shape. Rotation never widens the platform
// allowed set, never bypasses transport-confidence or release gating, and stays
// in-memory only. Disabled by default so the first landing is behaviour-neutral.
struct RuntimeProfileRotationPolicy final {
  bool enabled{false};
  uint32 failure_threshold{2};
  double quarantine_ttl_seconds{300.0};
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
  RuntimeProfileRotationPolicy profile_rotation;
  bool release_mode_profile_gating{false};
  bool require_per_install_selection_salt{false};
  TransportConfidence transport_confidence{TransportConfidence::Unknown};
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
