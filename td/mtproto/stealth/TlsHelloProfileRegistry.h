//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/stealth/Interfaces.h"

#include "td/utils/common.h"
#include "td/utils/Span.h"

#include <memory>

namespace td {

class KeyValueSyncInterface;

namespace mtproto {
namespace stealth {

enum class BrowserProfile : uint8 {
  Chrome133,
  Chrome131,
  Chrome120,
  Firefox148,
  Safari26_3,
  IOS14,
  Android11_OkHttp,
};

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
  uint8 firefox148{15};
  uint8 safari26_3{20};
  uint8 ios14{70};
  uint8 android11_okhttp{30};
};

struct ProfileSpec final {
  BrowserProfile id{BrowserProfile::Chrome133};
  Slice name;
  uint16 alps_type{0};
  bool allows_ech{false};
  bool allows_padding{false};
  bool has_session_id{true};
  bool has_pq{false};
  uint16 pq_group_id{0};
  ExtensionOrderPolicy extension_order_policy{ExtensionOrderPolicy::FixedFromFixture};
};

struct ProfileFixtureMetadata final {
  Slice source_id;
  ProfileFixtureSourceKind source_kind{ProfileFixtureSourceKind::UtlsSnapshot};
  ProfileTrustTier trust_tier{ProfileTrustTier::Advisory};
  bool has_independent_network_provenance{false};
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

RuntimePlatformHints default_runtime_platform_hints() noexcept;
SelectionKey make_profile_selection_key(Slice destination, int32 unix_time);
void set_runtime_ech_failure_store(std::shared_ptr<KeyValueSyncInterface> store);
void note_runtime_ech_decision(const RuntimeEchDecision &decision, bool ech_enabled) noexcept;
void note_runtime_ech_failure(Slice destination, int32 unix_time);
void note_runtime_ech_success(Slice destination, int32 unix_time);
void reset_runtime_ech_failure_state_for_tests();
RuntimeEchCounters get_runtime_ech_counters() noexcept;
void reset_runtime_ech_counters_for_tests() noexcept;
Span<BrowserProfile> all_profiles();
Span<BrowserProfile> allowed_profiles_for_platform(const RuntimePlatformHints &platform);
const ProfileSpec &profile_spec(BrowserProfile profile);
const ProfileFixtureMetadata &profile_fixture_metadata(BrowserProfile profile);
ProfileWeights default_profile_weights();
BrowserProfile pick_profile_sticky(const ProfileWeights &weights, const SelectionKey &key,
                                   const RuntimePlatformHints &platform, Span<BrowserProfile> allowed_profiles,
                                   IRng &rng);
BrowserProfile pick_runtime_profile(Slice destination, int32 unix_time, const RuntimePlatformHints &platform);
EchMode ech_mode_for_route(const NetworkRouteHints &route, const RouteFailureState &state) noexcept;
RuntimeEchDecision get_runtime_ech_decision(Slice destination, int32 unix_time,
                                            const NetworkRouteHints &route) noexcept;
EchMode runtime_ech_mode_for_route(Slice destination, int32 unix_time, const NetworkRouteHints &route) noexcept;

}  // namespace stealth
}  // namespace mtproto
}  // namespace td