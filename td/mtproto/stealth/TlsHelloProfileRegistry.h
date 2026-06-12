// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/stealth/Interfaces.h"

#include "td/utils/common.h"
#include "td/utils/Span.h"

#include <memory>

namespace td {

class KeyValueSyncInterface;

namespace mtproto {
namespace stealth {

using BrowserProfile = mtproto::BrowserProfile;

enum class DeviceClass : uint8 {
  Desktop,
  Mobile,
};

enum class MobileOs : uint8 {
  None,
  IOS,
  Android,
};

enum class DesktopOs : uint8 {
  Unknown,
  Darwin,
  Windows,
  Linux,
};

struct RuntimePlatformHints final {
  DeviceClass device_class{DeviceClass::Desktop};
  MobileOs mobile_os{MobileOs::None};
  DesktopOs desktop_os{DesktopOs::Unknown};
};

enum class EchMode : uint8 {
  Disabled = 0,
  Rfc9180Outer = 1,
};

enum class ExtensionOrderPolicy : uint8 {
  FixedFromFixture = 0,
  ChromeShuffleAnchored = 1,
};

enum class ProfileFixtureSourceKind : uint8 {
  BrowserCapture = 0,
  CurlCffiCapture = 1,
  UtlsSnapshot = 2,
  AdvisoryCodeSample = 3,
};

enum class ProfileTrustTier : uint8 {
  Advisory = 0,
  Verified = 1,
};

enum class TransportClaimLevel : uint8 {
  TlsOnly = 0,
  CrossLayerStrong = 1,
};

struct RouteFailureState final {
  bool ech_block_suspected{false};
  uint32 recent_ech_failures{0};
};

struct SelectionKey final {
  string destination;
  uint32 time_bucket{0};
};

struct ProfileWeights final {
  uint8 chrome133{50};
  uint8 chrome131{20};
  uint8 chrome120{15};
  uint8 chrome147_windows{50};
  uint8 chromium_macos_no_alps{10};
  uint8 chromium_macos_4469{25};
  uint8 chromium_macos_44cd{35};
  uint8 chrome147_ios_chromium{30};
  uint8 firefox148{15};
  uint8 firefox149_android{5};
  // Firefox 149 on macOS is a distinct fingerprint from Firefox 148 on Linux
  // (different cohort, ECH params). It gets its own weight slot so an operator
  // can tune or zero the macOS Firefox lane without also disabling the Linux
  // Firefox lane (and vice versa); previously both aliased `firefox148`.
  uint8 firefox149_macos26_3{10};
  uint8 firefox149_windows{15};
  uint8 safari26_3{20};
  uint8 ios14{70};
  // Verified browser-capture Apple iOS TLS lane. Carved from the iOS share in
  // effective_profile_weights_for_platform / flatten_profile_selection so it has
  // a non-zero effective weight and IOS14 is no longer the only Unknown-confidence
  // iOS lane. See AppleIosTls in BrowserProfile.h.
  uint8 apple_ios_tls{10};
  uint8 android_chromium_alps{20};
  uint8 android11_okhttp_advisory{10};
};

struct ProfileSpec final {
  BrowserProfile id{BrowserProfile::Chrome133};
  Slice name;
  uint16 alps_type{0};
  uint16 record_size_limit{0};
  bool allows_ech{false};
  bool allows_padding{false};
  bool has_session_id{true};
  bool has_pq{false};
  uint16 pq_group_id{0};
  uint8 ech_outer_type{0};
  uint16 ech_kdf_id{0x0001};
  uint16 ech_aead_id{0x0001};
  uint16 ech_payload_length{0};
  uint16 ech_enc_key_length{32};
  ExtensionOrderPolicy extension_order_policy{ExtensionOrderPolicy::FixedFromFixture};
};

struct ProfileFixtureMetadata final {
  Slice source_id;
  ProfileFixtureSourceKind source_kind{ProfileFixtureSourceKind::UtlsSnapshot};
  ProfileTrustTier trust_tier{ProfileTrustTier::Advisory};
  bool has_independent_network_provenance{false};
  bool has_utls_snapshot_corroboration{false};
  bool release_gating{false};
  TransportClaimLevel transport_claim_level{TransportClaimLevel::TlsOnly};
};

struct RuntimeEchDecision final {
  EchMode ech_mode{EchMode::Disabled};
  bool disabled_by_route{false};
  bool disabled_by_circuit_breaker{false};
  bool reenabled_after_ttl{false};
};

struct RuntimeEchCounters final {
  uint64 enabled_total{0};
  uint64 disabled_route_total{0};
  uint64 disabled_cb_total{0};
  uint64 reenabled_total{0};
};

struct RuntimeProfileSelectionCounters final {
  uint64 advisory_blocked_total{0};
};

// One quarantinable emitted wire variant: a destination's selected profile plus
// whether that hello actually carried ECH on the wire. Two route classes that
// converge to the same (profile, hello_uses_ech) pair share one quarantine unit.
struct RuntimeProfileWireVariant final {
  BrowserProfile profile{BrowserProfile::Chrome133};
  bool hello_uses_ech{false};
};

// Result of one adaptive selection for a single connection attempt. profile and
// hello_uses_ech together identify the exact emitted wire variant to account
// failure/success against.
struct RuntimeProfileSelectionDecision final {
  BrowserProfile profile{BrowserProfile::Chrome133};
  bool hello_uses_ech{false};
  bool avoided_quarantined_profile{false};
  uint32 quarantined_candidate_count{0};
};

// Typed, conservative failure attribution for profile quarantine. Only wire-shape
// rejections (MalformedHelloResponse, TransportRejectionAfterHello) are eligible.
// WrongRegime / ResponseHashMismatch / pre-hello (None) signals never quarantine:
// they indicate a wrong secret or protocol regime that no profile can repair, so
// rotating on them would be fingerprint roulette against a misconfigured proxy.
enum class RuntimeProfileFailureSignal : uint8 {
  None = 0,
  WrongRegime = 1,
  ResponseHashMismatch = 2,
  MalformedHelloResponse = 3,
  TransportRejectionAfterHello = 4,
};

struct RuntimeProfileRotationCounters final {
  uint64 profile_quarantine_hit_total{0};
  uint64 profile_quarantine_all_blocked_total{0};
  uint64 profile_failure_recorded_total{0};
  uint64 profile_success_cleared_total{0};
};

RuntimePlatformHints default_runtime_platform_hints() noexcept;
SelectionKey make_profile_selection_key(Slice destination, int32 unix_time);
void set_runtime_ech_failure_store(std::shared_ptr<KeyValueSyncInterface> store);
// Per-installation profile-selection salt. A non-zero salt de-correlates which
// profile installations on the same destination/platform/time bucket select.
// When a KV store is configured the salt is minted and persisted automatically
// (stable per install); a host may also set an externally persisted value.
// Passing 0 clears it (deterministic, no entropy). See effective_per_install_selection_salt.
void set_per_install_selection_salt(uint64 salt) noexcept;
uint64 get_per_install_selection_salt() noexcept;
void reset_per_install_selection_salt_for_tests() noexcept;
void reconcile_runtime_ech_failure_ttl(double ttl_seconds);
void note_runtime_ech_decision(const RuntimeEchDecision &decision, bool ech_enabled) noexcept;
void note_runtime_ech_failure(Slice destination, int32 unix_time);
void note_runtime_ech_success(Slice destination, int32 unix_time);
void reset_runtime_ech_failure_state_for_tests();
RuntimeEchCounters get_runtime_ech_counters() noexcept;
void reset_runtime_ech_counters_for_tests() noexcept;
RuntimeProfileSelectionCounters get_runtime_profile_selection_counters() noexcept;
void reset_runtime_profile_selection_counters_for_tests() noexcept;
Span<BrowserProfile> all_profiles();
Span<BrowserProfile> allowed_profiles_for_platform(const RuntimePlatformHints &platform);
const ProfileSpec &profile_spec(BrowserProfile profile);
const ProfileFixtureMetadata &profile_fixture_metadata(BrowserProfile profile);
ProfileWeights default_profile_weights();
BrowserProfile pick_profile_sticky(const ProfileWeights &weights, const SelectionKey &key,
                                   const RuntimePlatformHints &platform, Span<BrowserProfile> allowed_profiles,
                                   IRng &rng);
BrowserProfile pick_runtime_profile(Slice destination, int32 unix_time, const RuntimePlatformHints &platform);
// Adaptive single-attempt selection. Returns the legacy baseline unless the
// rotation policy is enabled AND the baseline's wire variant is quarantined for
// this destination, in which case it rotates to another already-allowed,
// non-quarantined wire variant (or stays fail-closed on the baseline if every
// candidate is quarantined). ech_mode is the route's resolved ECH decision.
RuntimeProfileSelectionDecision pick_runtime_profile_adaptive(Slice destination, int32 unix_time,
                                                              const RuntimePlatformHints &platform, EchMode ech_mode);
bool runtime_profile_failure_signal_is_quarantine_eligible(RuntimeProfileFailureSignal signal) noexcept;
void note_runtime_profile_failure(Slice destination, const RuntimeProfileWireVariant &variant,
                                  RuntimeProfileFailureSignal signal);
void note_runtime_profile_success(Slice destination, const RuntimeProfileWireVariant &variant);
RuntimeProfileRotationCounters get_runtime_profile_rotation_counters() noexcept;
void reset_runtime_profile_rotation_counters_for_tests() noexcept;
void reset_runtime_profile_quarantine_state_for_tests();
EchMode ech_mode_for_route(const NetworkRouteHints &route, const RouteFailureState &state) noexcept;
RuntimeEchDecision get_runtime_ech_decision(Slice destination, int32 unix_time,
                                            const NetworkRouteHints &route) noexcept;
EchMode runtime_ech_mode_for_route(Slice destination, int32 unix_time, const NetworkRouteHints &route) noexcept;

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
