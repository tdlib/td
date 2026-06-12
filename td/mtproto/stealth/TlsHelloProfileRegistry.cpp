// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "tddb/td/db/KeyValueSyncInterface.h"

#include "td/utils/Random.h"
#include "td/utils/crypto.h"
#include "td/utils/misc.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/Time.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace td {
namespace mtproto {
namespace stealth {
namespace tls_hello_profile_registry_internal {

constexpr BrowserProfile ALL_PROFILES[] = {
    BrowserProfile::Chrome133,
    BrowserProfile::Chrome131,
    BrowserProfile::Chrome120,
    BrowserProfile::Chrome147_Windows,
    BrowserProfile::Chrome147_IOSChromium,
    BrowserProfile::Firefox148,
    BrowserProfile::Firefox149_MacOS26_3,
    BrowserProfile::Firefox149_Windows,
    BrowserProfile::Safari26_3,
    BrowserProfile::IOS14,
    BrowserProfile::AndroidChromium_Alps,
    BrowserProfile::Android11_OkHttp_Advisory,
    BrowserProfile::ChromiumMacOS_NoAlps,
    BrowserProfile::ChromiumMacOS_4469,
    BrowserProfile::ChromiumMacOS_44CD,
    BrowserProfile::Firefox149_Android,
    BrowserProfile::AppleIosTls,
};

constexpr BrowserProfile DARWIN_DESKTOP_PROFILES[] = {
    BrowserProfile::ChromiumMacOS_NoAlps,
    BrowserProfile::ChromiumMacOS_4469,
    BrowserProfile::ChromiumMacOS_44CD,
    BrowserProfile::Safari26_3,
    BrowserProfile::Firefox149_MacOS26_3,
};

constexpr BrowserProfile NON_DARWIN_DESKTOP_PROFILES[] = {
    BrowserProfile::Chrome133,
    BrowserProfile::Chrome131,
    BrowserProfile::Chrome120,
    BrowserProfile::Firefox148,
};

constexpr BrowserProfile WINDOWS_DESKTOP_PROFILES[] = {
    BrowserProfile::Chrome147_Windows,
    BrowserProfile::Firefox149_Windows,
};

constexpr BrowserProfile MOBILE_PROFILES[] = {
    BrowserProfile::IOS14,
    BrowserProfile::Chrome147_IOSChromium,
    BrowserProfile::AppleIosTls,
    BrowserProfile::AndroidChromium_Alps,
    BrowserProfile::Firefox149_Android,
    BrowserProfile::Android11_OkHttp_Advisory,
};

constexpr BrowserProfile IOS_MOBILE_PROFILES[] = {
    BrowserProfile::IOS14,
    BrowserProfile::Chrome147_IOSChromium,
    BrowserProfile::AppleIosTls,
};

constexpr BrowserProfile ANDROID_MOBILE_PROFILES[] = {
    BrowserProfile::AndroidChromium_Alps,
    BrowserProfile::Firefox149_Android,
    BrowserProfile::Android11_OkHttp_Advisory,
};

constexpr Slice kPerInstallSelectionSaltStoreKey("stealth_profile_selection_salt");

constexpr ProfileSpec PROFILE_SPECS[] = {
    {BrowserProfile::Chrome133, Slice("chrome133"), 0x44CD, 0, true, true, true, true, 0x11EC, 0x00, 0x0001, 0x0001, 0,
     32, ExtensionOrderPolicy::ChromeShuffleAnchored},
    {BrowserProfile::Chrome131, Slice("chrome131"), 0x4469, 0, true, true, true, true, 0x11EC, 0x00, 0x0001, 0x0001, 0,
     32, ExtensionOrderPolicy::ChromeShuffleAnchored},
    {BrowserProfile::Chrome120, Slice("chrome120"), 0x4469, 0, true, true, true, false, 0, 0x00, 0x0001, 0x0001, 0, 32,
     ExtensionOrderPolicy::ChromeShuffleAnchored},
    // Windows Chromium family — Chrome 147 on Windows 10/11 uses the same
    // BoringSSL stack as Linux Chrome. Cipher suites, ALPS type (0x44CD),
    // PQ group (X25519MLKEM768=0x11EC), and ECH outer params are identical
    // to Chrome133. Separate profile entry for platform-gated selection and
    // chromium_windows family-lane matching. Sourced from 31 browser-capture
    // fixtures under test/analysis/fixtures/clienthello/windows/.
    {BrowserProfile::Chrome147_Windows, Slice("chrome147_windows"), 0x44CD, 0, true, true, true, true, 0x11EC, 0x00,
     0x0001, 0x0001, 0, 32, ExtensionOrderPolicy::ChromeShuffleAnchored},
    {BrowserProfile::Chrome147_IOSChromium, Slice("chrome147_ios_chromium"), 0x44CD, 0, true, false, true, true, 0x11EC,
     0x00, 0x0001, 0x0001, 144, 32, ExtensionOrderPolicy::FixedFromFixture},
    {BrowserProfile::Firefox148, Slice("firefox148"), 0, 0x4001, true, false, true, true, 0x11EC, 0x00, 0x0001, 0x0003,
     239, 32, ExtensionOrderPolicy::FixedFromFixture},
    {BrowserProfile::Firefox149_MacOS26_3, Slice("firefox149_macos26_3"), 0, 0x4001, true, false, true, true, 0x11EC,
     0x00, 0x0001, 0x0001, 399, 32, ExtensionOrderPolicy::FixedFromFixture},
    // Windows Firefox family — Firefox 149 on Windows 10/11 uses Gecko/NSS,
    // identical TLS stack to Linux Firefox. Extension set includes ECH outer
    // (0xFE0D), PQ key share (0x11EC). Sourced from browser-capture fixtures
    // under test/analysis/fixtures/clienthello/windows/.
    {BrowserProfile::Firefox149_Windows, Slice("firefox149_windows"), 0, 0x4001, true, false, true, true, 0x11EC, 0x00,
     0x0001, 0x0003, 239, 32, ExtensionOrderPolicy::FixedFromFixture},
    // Apple TLS family — Safari 26.x and iOS 14 (which represents the
    // current iOS 26.x Apple TLS family despite the legacy enum name)
    // both adopted X25519MLKEM768. Real captures under
    // test/analysis/fixtures/clienthello/ios/{safari26_*,chrome147_*}.json
    // advertise {0x11EC, 0x001D, 0x0017, 0x0018, 0x0019} in
    // supported_groups and a hybrid (0x11EC) + classical (0x001D) pair
    // in key_share. The legacy ProfileSpec `has_pq`/`pq_group_id`
    // fields are kept in sync with `BrowserProfileSpec` by
    // `test_profile_spec_pq_consistency_invariants.cpp`.
    {BrowserProfile::Safari26_3, Slice("safari26_3"), 0, 0, false, false, true, true, 0x11EC, 0x00, 0x0001, 0x0001, 0,
     32, ExtensionOrderPolicy::FixedFromFixture},
    {BrowserProfile::IOS14, Slice("ios14"), 0, 0, false, false, true, true, 0x11EC, 0x00, 0x0001, 0x0001, 0, 32,
     ExtensionOrderPolicy::FixedFromFixture},
    {BrowserProfile::AndroidChromium_Alps, Slice("android_chromium_alps"), 0x44CD, 0, true, true, true, true, 0x11EC,
     0x00, 0x0001, 0x0001, 0, 32, ExtensionOrderPolicy::ChromeShuffleAnchored},
    {BrowserProfile::Android11_OkHttp_Advisory, Slice("android11_okhttp_advisory"), 0, 0, false, false, true, false, 0,
     0x00, 0x0001, 0x0001, 0, 32, ExtensionOrderPolicy::FixedFromFixture},
    {BrowserProfile::ChromiumMacOS_NoAlps, Slice("chromium_macos_no_alps"), 0, 0, true, true, true, true, 0x11EC,
     0x00, 0x0001, 0x0001, 0, 32, ExtensionOrderPolicy::ChromeShuffleAnchored},
    {BrowserProfile::ChromiumMacOS_4469, Slice("chromium_macos_4469"), 0x4469, 0, true, true, true, true, 0x11EC,
     0x00, 0x0001, 0x0001, 0, 32, ExtensionOrderPolicy::ChromeShuffleAnchored},
    {BrowserProfile::ChromiumMacOS_44CD, Slice("chromium_macos_44cd"), 0x44CD, 0, true, true, true, true, 0x11EC,
     0x00, 0x0001, 0x0001, 0, 32, ExtensionOrderPolicy::ChromeShuffleAnchored},
    {BrowserProfile::Firefox149_Android, Slice("firefox149_android"), 0, 0x4001, true, false, true, true, 0x11EC,
     0x00, 0x0001, 0x0001, 399, 32, ExtensionOrderPolicy::FixedFromFixture},
    // Reviewed Apple iOS TLS family lane. Wire image is the proven iOS Apple TLS
    // family shape (same has_pq/pq_group_id=X25519MLKEM768, no ECH — matches
    // make_apple_ios_tls_impl, which is anchored to make_ios14_impl). Its trust
    // metadata in PROFILE_FIXTURES, not this wire spec, is what makes it the
    // verified release-gated iOS lane.
    {BrowserProfile::AppleIosTls, Slice("apple_ios_tls"), 0, 0, false, false, true, true, 0x11EC, 0x00, 0x0001, 0x0001,
     0, 32, ExtensionOrderPolicy::FixedFromFixture},
};

constexpr ProfileFixtureMetadata PROFILE_FIXTURES[] = {
    {Slice("curl_cffi:chrome133"), ProfileFixtureSourceKind::CurlCffiCapture, ProfileTrustTier::Verified, true, true,
     true, TransportClaimLevel::CrossLayerStrong},
    {Slice("curl_cffi:chrome131"), ProfileFixtureSourceKind::CurlCffiCapture, ProfileTrustTier::Verified, true, true,
     true, TransportClaimLevel::CrossLayerStrong},
    {Slice("browser_capture:chrome120_non_pq"), ProfileFixtureSourceKind::BrowserCapture, ProfileTrustTier::Verified,
     true, true, true, TransportClaimLevel::TlsOnly},
    {Slice("browser_capture:chrome147_windows"), ProfileFixtureSourceKind::BrowserCapture, ProfileTrustTier::Verified,
     true, true, false, TransportClaimLevel::CrossLayerStrong},
    {Slice("browser_capture:chrome147_ios_chromium"), ProfileFixtureSourceKind::BrowserCapture,
     ProfileTrustTier::Verified, true, false, false, TransportClaimLevel::CrossLayerStrong},
    {Slice("browser_capture:firefox148"), ProfileFixtureSourceKind::BrowserCapture, ProfileTrustTier::Verified, true,
     true, true, TransportClaimLevel::TlsOnly},
    {Slice("browser_capture:firefox149_macos26_3"), ProfileFixtureSourceKind::BrowserCapture,
     ProfileTrustTier::Verified, true, true, true, TransportClaimLevel::CrossLayerStrong},
    {Slice("browser_capture:firefox149_windows"), ProfileFixtureSourceKind::BrowserCapture, ProfileTrustTier::Verified,
     true, true, false, TransportClaimLevel::TlsOnly},
    {Slice("utls:HelloSafari_26_3"), ProfileFixtureSourceKind::UtlsSnapshot, ProfileTrustTier::Advisory, false, false,
     false, TransportClaimLevel::TlsOnly},
    {Slice("utls:HelloIOS_14"), ProfileFixtureSourceKind::UtlsSnapshot, ProfileTrustTier::Advisory, false, false, false,
     TransportClaimLevel::TlsOnly},
    {Slice("browser_capture:android_chromium_alps"), ProfileFixtureSourceKind::BrowserCapture,
     ProfileTrustTier::Verified, true, false, true, TransportClaimLevel::CrossLayerStrong},
    {Slice("utls:HelloAndroid_11_OkHttp"), ProfileFixtureSourceKind::UtlsSnapshot, ProfileTrustTier::Advisory, false,
     false, false, TransportClaimLevel::TlsOnly},
    {Slice("browser_capture:chromium_macos_no_alps"), ProfileFixtureSourceKind::BrowserCapture,
     ProfileTrustTier::Verified, true, false, true, TransportClaimLevel::TlsOnly},
    {Slice("browser_capture:chromium_macos_4469"), ProfileFixtureSourceKind::BrowserCapture,
     ProfileTrustTier::Verified, true, false, true, TransportClaimLevel::CrossLayerStrong},
    {Slice("browser_capture:chromium_macos_44cd"), ProfileFixtureSourceKind::BrowserCapture,
     ProfileTrustTier::Verified, true, false, true, TransportClaimLevel::CrossLayerStrong},
    {Slice("browser_capture:firefox149_android"), ProfileFixtureSourceKind::BrowserCapture, ProfileTrustTier::Verified,
     true, false, false, TransportClaimLevel::CrossLayerStrong},
    // Apple iOS TLS verified lane: browser-capture provenance, release-gated, and
    // a TlsOnly transport claim so it is reachable at TransportConfidence::Unknown.
    // This is the conjunction (TlsOnly + release_gating + Verified) the advisory
    // IOS14 and CrossLayerStrong Chrome147_IOSChromium lanes cannot satisfy.
    {Slice("browser_capture:apple_ios_tls"), ProfileFixtureSourceKind::BrowserCapture, ProfileTrustTier::Verified, true,
     false, true, TransportClaimLevel::TlsOnly},
};

// Compile-time alignment guard. ALL_PROFILES, PROFILE_SPECS, and PROFILE_FIXTURES
// are bound to the BrowserProfile enum only by array position, because
// profile_index(p) == static_cast<size_t>(p) indexes PROFILE_SPECS/PROFILE_FIXTURES
// directly. A mis-insertion or reordering would silently hand back the wrong wire
// spec or trust metadata for a profile (e.g. an advisory lane inheriting a
// release-gated, Verified fixture and defeating the release gate). Pin the
// invariant at build time so any future profile edit that breaks position fails to
// compile instead of corrupting selection at runtime. (PROFILE_FIXTURES carries no
// id field, so it is count-checked here and kept in lockstep with PROFILE_SPECS;
// giving it an explicit BrowserProfile id is a recommended follow-up.)
constexpr size_t kRegisteredProfileCount = sizeof(ALL_PROFILES) / sizeof(ALL_PROFILES[0]);

constexpr bool profile_registry_arrays_are_index_aligned() {
  if (sizeof(PROFILE_SPECS) / sizeof(PROFILE_SPECS[0]) != kRegisteredProfileCount) {
    return false;
  }
  if (sizeof(PROFILE_FIXTURES) / sizeof(PROFILE_FIXTURES[0]) != kRegisteredProfileCount) {
    return false;
  }
  for (size_t i = 0; i < kRegisteredProfileCount; i++) {
    if (static_cast<size_t>(ALL_PROFILES[i]) != i) {
      return false;
    }
    if (PROFILE_SPECS[i].id != ALL_PROFILES[i]) {
      return false;
    }
  }
  return true;
}

static_assert(profile_registry_arrays_are_index_aligned(),
              "BrowserProfile enum, ALL_PROFILES, PROFILE_SPECS, and PROFILE_FIXTURES must stay index-aligned: "
              "profile_index(p) == static_cast<size_t>(p) positionally indexes PROFILE_SPECS and PROFILE_FIXTURES");

constexpr Slice kRuntimeEchStoreKeyPrefix("stealth_ech_cb#");
constexpr uint32 kRouteFailureKeyBucketSeconds = 86400;
constexpr size_t kMaxRouteFailureAliasDotsForLookup = 8;

struct RouteFailureCacheEntry final {
  RouteFailureState state;
  Timestamp disabled_until;
};

struct CasefoldStoreLookupCandidate final {
  string key;
  string value;
  bool is_legacy{false};
  bool from_previous_bucket{false};
};

struct RuntimeEchCounterStorage final {
  std::atomic<uint64> enabled_total{0};
  std::atomic<uint64> disabled_route_total{0};
  std::atomic<uint64> disabled_cb_total{0};
  std::atomic<uint64> reenabled_total{0};
};

struct RuntimeProfileSelectionCounterStorage final {
  std::atomic<uint64> advisory_blocked_total{0};
};

std::mutex &route_failure_cache_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<string, RouteFailureCacheEntry> &route_failure_cache() {
  static std::unordered_map<string, RouteFailureCacheEntry> cache;
  return cache;
}

RuntimeActivePolicy active_policy_for_route(const StealthRuntimeParams &runtime_params,
                                            const NetworkRouteHints &route) noexcept {
  if (route.is_known) {
    return route.is_ru ? RuntimeActivePolicy::RuEgress : RuntimeActivePolicy::NonRuEgress;
  }
  return runtime_params.active_policy;
}

const RuntimeRoutePolicyEntry &route_policy_entry_for_active_policy(const StealthRuntimeParams &runtime_params,
                                                                    RuntimeActivePolicy active_policy) noexcept {
  switch (active_policy) {
    case RuntimeActivePolicy::RuEgress:
      return runtime_params.route_policy.ru;
    case RuntimeActivePolicy::NonRuEgress:
      return runtime_params.route_policy.non_ru;
    case RuntimeActivePolicy::Unknown:
    default:
      return runtime_params.route_policy.unknown;
  }
}

std::shared_ptr<KeyValueSyncInterface> &route_failure_store() {
  static std::shared_ptr<KeyValueSyncInterface> store;
  return store;
}

RuntimeEchCounterStorage &runtime_ech_counters() {
  static RuntimeEchCounterStorage counters;
  return counters;
}

RuntimeProfileSelectionCounterStorage &runtime_profile_selection_counters() {
  static RuntimeProfileSelectionCounterStorage counters;
  return counters;
}

RouteFailureState fail_closed_route_failure_state() {
  auto runtime_params = get_runtime_stealth_params_snapshot();
  RouteFailureState state;
  state.ech_block_suspected = true;
  state.recent_ech_failures = runtime_params.route_failure.ech_failure_threshold;
  return state;
}

RouteFailureCacheEntry make_fail_closed_route_failure_cache_entry() {
  auto runtime_params = get_runtime_stealth_params_snapshot();
  RouteFailureCacheEntry entry;
  entry.state = fail_closed_route_failure_state();
  entry.disabled_until = Timestamp::in(runtime_params.route_failure.ech_disable_ttl_seconds);
  return entry;
}

size_t profile_index(BrowserProfile profile) {
  // Fail fast on an out-of-enum value instead of an out-of-bounds read into the
  // positional PROFILE_SPECS/PROFILE_FIXTURES arrays.
  CHECK(static_cast<size_t>(profile) < kRegisteredProfileCount);
  return static_cast<size_t>(profile);
}

string normalized_runtime_destination_key(Slice destination) {
  auto key = destination.substr(0, ProxySecret::MAX_DOMAIN_LENGTH).str();
  while (!key.empty() && key.back() == '.') {
    key.pop_back();
  }
  // Domain names are case-insensitive. Canonicalize to lowercase so
  // profile stickiness and ECH failure-cache keys cannot be bypassed by
  // case-only aliases of the same destination.
  for (auto &ch : key) {
    if ('A' <= ch && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return key;
}

bool has_runtime_destination_identity(Slice destination) {
  return !normalized_runtime_destination_key(destination).empty();
}

string runtime_destination_key_preserving_case(Slice destination) {
  auto key = destination.substr(0, ProxySecret::MAX_DOMAIN_LENGTH).str();
  while (!key.empty() && key.back() == '.') {
    key.pop_back();
  }
  return key;
}

string uppercase_runtime_destination_key(Slice destination) {
  auto key = normalized_runtime_destination_key(destination);
  for (auto &ch : key) {
    if ('a' <= ch && ch <= 'z') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }
  }
  return key;
}

uint32 legacy_route_failure_bucket(int32 unix_time) {
  auto unix_time64 = static_cast<int64>(unix_time);
  if (unix_time64 < 0) {
    unix_time64 = 0;
  }

  return static_cast<uint32>(unix_time64 / kRouteFailureKeyBucketSeconds);
}

string route_failure_cache_key(Slice destination, int32 unix_time) {
  static_cast<void>(unix_time);

  return normalized_runtime_destination_key(destination);
}

string route_failure_store_key(Slice destination, int32 unix_time) {
  return kRuntimeEchStoreKeyPrefix.str() + route_failure_cache_key(destination, unix_time);
}

td::vector<string> route_failure_store_keys_for_lookup(Slice destination, int32 unix_time) {
  static_cast<void>(unix_time);

  td::vector<string> keys;
  auto canonical_destination_key = normalized_runtime_destination_key(destination);
  auto preserved_destination_key = runtime_destination_key_preserving_case(destination);
  auto uppercase_destination_key = uppercase_runtime_destination_key(destination);
  auto max_alias_dots = ProxySecret::MAX_DOMAIN_LENGTH > canonical_destination_key.size()
                            ? ProxySecret::MAX_DOMAIN_LENGTH - canonical_destination_key.size()
                            : 0;
  max_alias_dots = std::min(max_alias_dots, kMaxRouteFailureAliasDotsForLookup);
  td::vector<string> destination_variants;
  destination_variants.reserve(3);
  destination_variants.push_back(canonical_destination_key);
  if (preserved_destination_key != canonical_destination_key) {
    destination_variants.push_back(preserved_destination_key);
  }
  if (uppercase_destination_key != canonical_destination_key &&
      uppercase_destination_key != preserved_destination_key) {
    destination_variants.push_back(uppercase_destination_key);
  }

  auto variant_count = destination_variants.size();
  keys.reserve((1 + max_alias_dots) * variant_count);

  auto add_destination_keys = [&](const string &destination_key) {
    keys.push_back(kRuntimeEchStoreKeyPrefix.str() + destination_key);
    if (!destination_key.empty()) {
      auto dotted_destination_key = destination_key;
      for (size_t dots = 1; dots <= max_alias_dots; dots++) {
        dotted_destination_key += '.';
        keys.push_back(kRuntimeEchStoreKeyPrefix.str() + dotted_destination_key);
      }
    }
  };

  for (const auto &destination_key : destination_variants) {
    add_destination_keys(destination_key);
  }
  return keys;
}

td::vector<string> legacy_route_failure_store_keys_for_lookup(Slice destination, int32 unix_time) {
  auto bucket = legacy_route_failure_bucket(unix_time);
  auto canonical_destination_key = normalized_runtime_destination_key(destination);
  auto preserved_destination_key = runtime_destination_key_preserving_case(destination);
  auto uppercase_destination_key = uppercase_runtime_destination_key(destination);
  auto max_alias_dots = ProxySecret::MAX_DOMAIN_LENGTH > canonical_destination_key.size()
                            ? ProxySecret::MAX_DOMAIN_LENGTH - canonical_destination_key.size()
                            : 0;
  max_alias_dots = std::min(max_alias_dots, kMaxRouteFailureAliasDotsForLookup);

  td::vector<string> keys;
  td::vector<string> destination_variants;
  destination_variants.reserve(3);
  destination_variants.push_back(canonical_destination_key);
  if (preserved_destination_key != canonical_destination_key) {
    destination_variants.push_back(preserved_destination_key);
  }
  if (uppercase_destination_key != canonical_destination_key &&
      uppercase_destination_key != preserved_destination_key) {
    destination_variants.push_back(uppercase_destination_key);
  }

  auto variant_count = destination_variants.size();
  keys.reserve((1 + max_alias_dots) * (bucket > 0 ? 2 : 1) * variant_count);

  auto add_bucket_keys = [&](const string &destination_key) {
    keys.push_back(kRuntimeEchStoreKeyPrefix.str() + destination_key + "|" + std::to_string(bucket));
    if (bucket > 0) {
      keys.push_back(kRuntimeEchStoreKeyPrefix.str() + destination_key + "|" + std::to_string(bucket - 1));
    }
  };

  for (const auto &destination_key : destination_variants) {
    add_bucket_keys(destination_key);
    if (!destination_key.empty()) {
      auto dotted_destination_key = destination_key;
      for (size_t dots = 1; dots <= max_alias_dots; dots++) {
        dotted_destination_key += '.';
        add_bucket_keys(dotted_destination_key);
      }
    }
  }
  return keys;
}

string serialize_route_failure_cache_entry(const RouteFailureCacheEntry &entry) {
  auto remaining_ms = int64{0};
  if (entry.disabled_until) {
    remaining_ms = max(static_cast<int64>((entry.disabled_until.at() - Time::now()) * 1000.0), int64{0});
  }
  auto system_ms = static_cast<int64>(Clocks::system() * 1000.0);

  string serialized = std::to_string(entry.state.recent_ech_failures);
  serialized += '|';
  serialized += entry.state.ech_block_suspected ? '1' : '0';
  serialized += '|';
  serialized += std::to_string(remaining_ms);
  serialized += '|';
  serialized += std::to_string(system_ms);
  return serialized;
}

bool parse_int64_str(Slice value, int64 &result) {
  auto text = value.str();
  if (text.empty()) {
    return false;
  }
  errno = 0;
  char *end = nullptr;
  auto parsed = std::strtoll(text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || errno == ERANGE) {
    return false;
  }
  result = static_cast<int64>(parsed);
  return true;
}

bool parse_uint32_str(Slice value, uint32 &result) {
  int64 parsed = 0;
  if (!parse_int64_str(value, parsed) || parsed < 0 || parsed > std::numeric_limits<uint32>::max()) {
    return false;
  }
  result = static_cast<uint32>(parsed);
  return true;
}

bool parse_route_failure_cache_entry(Slice serialized, RouteFailureCacheEntry &entry) {
  auto first = serialized.find('|');
  if (first == Slice::npos) {
    return false;
  }
  auto second = serialized.substr(first + 1).find('|');
  if (second == Slice::npos) {
    return false;
  }
  second += first + 1;
  auto third = serialized.substr(second + 1).find('|');
  if (third == Slice::npos) {
    return false;
  }
  third += second + 1;

  auto failures_str = serialized.substr(0, first);
  auto blocked_str = serialized.substr(first + 1, second - first - 1);
  auto remaining_ms_str = serialized.substr(second + 1, third - second - 1);
  auto system_ms_str = serialized.substr(third + 1);

  if (failures_str.empty() || blocked_str.size() != 1 || remaining_ms_str.empty() || system_ms_str.empty()) {
    return false;
  }

  if (!parse_uint32_str(failures_str, entry.state.recent_ech_failures)) {
    return false;
  }
  if (blocked_str[0] != '0' && blocked_str[0] != '1') {
    return false;
  }
  entry.state.ech_block_suspected = blocked_str[0] == '1';

  int64 stored_remaining_ms = 0;
  int64 stored_system_ms = 0;
  if (!parse_int64_str(remaining_ms_str, stored_remaining_ms) || !parse_int64_str(system_ms_str, stored_system_ms)) {
    return false;
  }
  if (stored_remaining_ms < 0 || stored_system_ms < 0) {
    return false;
  }
  auto elapsed_ms = max(static_cast<int64>(Clocks::system() * 1000.0) - stored_system_ms, int64{0});
  auto effective_remaining_ms = max(stored_remaining_ms - elapsed_ms, int64{0});
  if (effective_remaining_ms > 0) {
    entry.disabled_until = Timestamp::in(static_cast<double>(effective_remaining_ms) / 1000.0);
  }
  return entry.state.recent_ech_failures != 0 || entry.state.ech_block_suspected;
}

bool parse_casefold_store_lookup_candidate_key(Slice store_key, string &destination_key, bool &is_legacy,
                                               bool &from_previous_bucket, int32 unix_time) {
  if (store_key.size() < kRuntimeEchStoreKeyPrefix.size() ||
      store_key.substr(0, kRuntimeEchStoreKeyPrefix.size()) != kRuntimeEchStoreKeyPrefix) {
    return false;
  }

  auto suffix = store_key.substr(kRuntimeEchStoreKeyPrefix.size()).str();
  auto bucket_delim_pos = suffix.rfind('|');
  if (bucket_delim_pos == string::npos) {
    destination_key = std::move(suffix);
    is_legacy = false;
    from_previous_bucket = false;
    return true;
  }

  auto bucket_text = Slice(suffix).substr(bucket_delim_pos + 1);
  uint32 parsed_bucket = 0;
  if (!parse_uint32_str(bucket_text, parsed_bucket)) {
    return false;
  }

  auto current_bucket = legacy_route_failure_bucket(unix_time);
  bool current_bucket_match = parsed_bucket == current_bucket;
  bool previous_bucket_match = current_bucket > 0 && parsed_bucket == current_bucket - 1;
  if (!current_bucket_match && !previous_bucket_match) {
    return false;
  }

  destination_key = suffix.substr(0, bucket_delim_pos);
  is_legacy = true;
  from_previous_bucket = previous_bucket_match;
  return true;
}

td::vector<CasefoldStoreLookupCandidate> collect_casefold_store_lookup_candidates_locked(Slice destination,
                                                                                         int32 unix_time) {
  td::vector<CasefoldStoreLookupCandidate> candidates;
  auto store = route_failure_store();
  if (store == nullptr) {
    return candidates;
  }

  auto canonical_destination_key = normalized_runtime_destination_key(destination);
  auto direct_preserved_destination_key = runtime_destination_key_preserving_case(destination);
  auto direct_uppercase_destination_key = uppercase_runtime_destination_key(destination);

  auto entries = store->prefix_get(kRuntimeEchStoreKeyPrefix);
  candidates.reserve(entries.size());
  for (const auto &it : entries) {
    string candidate_destination_key;
    bool candidate_is_legacy = false;
    bool candidate_from_previous_bucket = false;
    if (!parse_casefold_store_lookup_candidate_key(it.first, candidate_destination_key, candidate_is_legacy,
                                                   candidate_from_previous_bucket, unix_time)) {
      continue;
    }
    if (normalized_runtime_destination_key(candidate_destination_key) != canonical_destination_key) {
      continue;
    }

    // Direct lookups already handle canonical, query-preserved, and uppercase
    // aliases (with dotted variants). The fallback is strictly for remaining
    // mixed-case historical keys.
    auto candidate_exact = Slice(candidate_destination_key);
    if (candidate_exact == canonical_destination_key || candidate_exact == direct_preserved_destination_key ||
        candidate_exact == direct_uppercase_destination_key) {
      continue;
    }

    CasefoldStoreLookupCandidate candidate;
    candidate.key = it.first;
    candidate.value = it.second;
    candidate.is_legacy = candidate_is_legacy;
    candidate.from_previous_bucket = candidate_from_previous_bucket;
    candidates.push_back(std::move(candidate));
  }

  auto candidate_rank = [](const CasefoldStoreLookupCandidate &candidate) {
    if (!candidate.is_legacy) {
      return 0;
    }
    if (candidate.from_previous_bucket) {
      return 2;
    }
    return 1;
  };

  std::ranges::sort(candidates, [&](const auto &lhs, const auto &rhs) {
    auto lhs_rank = candidate_rank(lhs);
    auto rhs_rank = candidate_rank(rhs);
    if (lhs_rank != rhs_rank) {
      return lhs_rank < rhs_rank;
    }
    return lhs.key < rhs.key;
  });
  return candidates;
}

void erase_casefold_store_lookup_candidates_locked(const td::vector<CasefoldStoreLookupCandidate> &candidates) {
  auto store = route_failure_store();
  if (store == nullptr) {
    return;
  }

  for (const auto &candidate : candidates) {
    store->erase(candidate.key);
  }
}

bool try_load_casefold_route_failure_cache_entry_locked(Slice destination, int32 unix_time,
                                                        RouteFailureCacheEntry &entry,
                                                        bool *saw_expired_blocked_entry = nullptr) {
  auto store = route_failure_store();
  if (store == nullptr) {
    return false;
  }

  auto candidates = collect_casefold_store_lookup_candidates_locked(destination, unix_time);
  for (const auto &candidate : candidates) {
    if (candidate.value.empty()) {
      store->erase(candidate.key);
      continue;
    }

    if (!parse_route_failure_cache_entry(candidate.value, entry)) {
      // Erase only the malformed candidate and keep searching. Erasing all
      // candidates here would silently truncate a longer-lived active-block
      // that follows this entry in rank order, replacing it with the short
      // fail_closed TTL.
      store->erase(candidate.key);
      continue;
    }
    if (!entry.disabled_until || entry.disabled_until.is_in_past()) {
      if (saw_expired_blocked_entry != nullptr && entry.state.ech_block_suspected) {
        *saw_expired_blocked_entry = true;
      }
      store->erase(candidate.key);
      continue;
    }

    erase_casefold_store_lookup_candidates_locked(candidates);
    return true;
  }

  return false;
}

bool clamp_route_failure_disabled_until(RouteFailureCacheEntry &entry, Timestamp max_disabled_until) {
  if (!entry.disabled_until || entry.disabled_until.is_in_past()) {
    return false;
  }
  if (entry.disabled_until.at() <= max_disabled_until.at()) {
    return false;
  }
  entry.disabled_until = max_disabled_until;
  return true;
}

void persist_route_failure_cache_entry_locked(Slice destination, int32 unix_time, const RouteFailureCacheEntry &entry) {
  auto store = route_failure_store();
  if (store == nullptr) {
    return;
  }
  if (entry.state.recent_ech_failures == 0 && !entry.state.ech_block_suspected) {
    store->erase(route_failure_store_key(destination, unix_time));
    return;
  }
  store->set(route_failure_store_key(destination, unix_time), serialize_route_failure_cache_entry(entry));
}

void erase_route_failure_cache_entry_locked(Slice destination, int32 unix_time) {
  auto store = route_failure_store();
  if (store == nullptr) {
    return;
  }
  for (const auto &key : route_failure_store_keys_for_lookup(destination, unix_time)) {
    store->erase(key);
  }
}

void erase_legacy_route_failure_cache_entries_locked(Slice destination, int32 unix_time) {
  auto store = route_failure_store();
  if (store == nullptr) {
    return;
  }
  for (const auto &key : legacy_route_failure_store_keys_for_lookup(destination, unix_time)) {
    store->erase(key);
  }
}

bool try_load_legacy_route_failure_cache_entry_locked(Slice destination, int32 unix_time, RouteFailureCacheEntry &entry,
                                                      bool *saw_expired_blocked_entry = nullptr) {
  auto store = route_failure_store();
  if (store == nullptr) {
    return false;
  }

  for (const auto &legacy_key : legacy_route_failure_store_keys_for_lookup(destination, unix_time)) {
    auto serialized = store->get(legacy_key);
    if (serialized.empty()) {
      continue;
    }

    if (!parse_route_failure_cache_entry(serialized, entry)) {
      entry = make_fail_closed_route_failure_cache_entry();
      erase_legacy_route_failure_cache_entries_locked(destination, unix_time);
      return true;
    }
    if (!entry.disabled_until || entry.disabled_until.is_in_past()) {
      if (saw_expired_blocked_entry != nullptr && entry.state.ech_block_suspected) {
        *saw_expired_blocked_entry = true;
      }
      store->erase(legacy_key);
      continue;
    }

    erase_legacy_route_failure_cache_entries_locked(destination, unix_time);
    return true;
  }

  return false;
}

RouteFailureState get_runtime_route_failure_state_locked(Slice destination, int32 unix_time,
                                                         bool *saw_expired_blocked_entry,
                                                         Timestamp max_disabled_until) {
  auto mark_expired_blocked_entry = [&](const RouteFailureCacheEntry &entry) {
    if (saw_expired_blocked_entry != nullptr && entry.state.ech_block_suspected) {
      *saw_expired_blocked_entry = true;
    }
  };

  auto &cache = route_failure_cache();
  auto key = route_failure_cache_key(destination, unix_time);
  auto it = cache.find(key);
  if (it == cache.end()) {
    auto store = route_failure_store();
    if (store == nullptr) {
      return RouteFailureState{};
    }
    auto canonical_store_key = route_failure_store_key(destination, unix_time);
    RouteFailureCacheEntry entry;
    for (const auto &store_key : route_failure_store_keys_for_lookup(destination, unix_time)) {
      auto serialized = store->get(store_key);
      if (serialized.empty()) {
        continue;
      }
      if (!parse_route_failure_cache_entry(serialized, entry)) {
        entry = make_fail_closed_route_failure_cache_entry();
        clamp_route_failure_disabled_until(entry, max_disabled_until);
        auto inserted = cache.emplace(key, entry);
        persist_route_failure_cache_entry_locked(destination, unix_time, inserted.first->second);
        if (store_key != canonical_store_key) {
          store->erase(store_key);
        }
        return inserted.first->second.state;
      }
      clamp_route_failure_disabled_until(entry, max_disabled_until);
      if (!entry.disabled_until || entry.disabled_until.is_in_past()) {
        mark_expired_blocked_entry(entry);
        store->erase(store_key);
        continue;
      }
      auto inserted = cache.emplace(key, entry);
      persist_route_failure_cache_entry_locked(destination, unix_time, inserted.first->second);
      if (store_key != canonical_store_key) {
        store->erase(store_key);
      }
      return inserted.first->second.state;
    }
    if (try_load_legacy_route_failure_cache_entry_locked(destination, unix_time, entry, saw_expired_blocked_entry)) {
      clamp_route_failure_disabled_until(entry, max_disabled_until);
      auto inserted = cache.emplace(key, entry);
      persist_route_failure_cache_entry_locked(destination, unix_time, inserted.first->second);
      return inserted.first->second.state;
    }
    if (try_load_casefold_route_failure_cache_entry_locked(destination, unix_time, entry, saw_expired_blocked_entry)) {
      clamp_route_failure_disabled_until(entry, max_disabled_until);
      auto inserted = cache.emplace(key, entry);
      persist_route_failure_cache_entry_locked(destination, unix_time, inserted.first->second);
      return inserted.first->second.state;
    }
    return RouteFailureState{};
  }
  if (clamp_route_failure_disabled_until(it->second, max_disabled_until)) {
    persist_route_failure_cache_entry_locked(destination, unix_time, it->second);
  }
  if (it->second.disabled_until && it->second.disabled_until.is_in_past()) {
    mark_expired_blocked_entry(it->second);
    cache.erase(it);

    auto store = route_failure_store();
    if (store != nullptr) {
      auto canonical_store_key = route_failure_store_key(destination, unix_time);
      for (const auto &store_key : route_failure_store_keys_for_lookup(destination, unix_time)) {
        auto serialized = store->get(store_key);
        if (serialized.empty()) {
          continue;
        }
        RouteFailureCacheEntry entry;
        if (!parse_route_failure_cache_entry(serialized, entry)) {
          entry = make_fail_closed_route_failure_cache_entry();
          clamp_route_failure_disabled_until(entry, max_disabled_until);
          auto inserted = cache.emplace(key, entry);
          persist_route_failure_cache_entry_locked(destination, unix_time, inserted.first->second);
          if (store_key != canonical_store_key) {
            store->erase(store_key);
          }
          return inserted.first->second.state;
        }
        clamp_route_failure_disabled_until(entry, max_disabled_until);
        if (entry.disabled_until && !entry.disabled_until.is_in_past()) {
          auto inserted = cache.emplace(key, entry);
          persist_route_failure_cache_entry_locked(destination, unix_time, inserted.first->second);
          if (store_key != canonical_store_key) {
            store->erase(store_key);
          }
          return inserted.first->second.state;
        }
        mark_expired_blocked_entry(entry);
        store->erase(store_key);
      }
    }

    RouteFailureCacheEntry entry;
    if (try_load_legacy_route_failure_cache_entry_locked(destination, unix_time, entry, saw_expired_blocked_entry)) {
      clamp_route_failure_disabled_until(entry, max_disabled_until);
      auto inserted = cache.emplace(key, entry);
      persist_route_failure_cache_entry_locked(destination, unix_time, inserted.first->second);
      return inserted.first->second.state;
    }
    if (try_load_casefold_route_failure_cache_entry_locked(destination, unix_time, entry, saw_expired_blocked_entry)) {
      clamp_route_failure_disabled_until(entry, max_disabled_until);
      auto inserted = cache.emplace(key, entry);
      persist_route_failure_cache_entry_locked(destination, unix_time, inserted.first->second);
      return inserted.first->second.state;
    }
    return RouteFailureState{};
  }
  return it->second.state;
}

uint8 profile_weight(const ProfileWeights &weights, BrowserProfile profile) {
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

std::atomic<uint64> &per_install_selection_salt_cache() {
  static std::atomic<uint64> salt{0};
  return salt;
}

Result<uint64> parse_persisted_per_install_selection_salt(Slice encoded_salt) {
  if (encoded_salt.empty()) {
    return Status::Error("missing persisted per-install selection salt");
  }
  TRY_RESULT(salt, to_integer_safe<uint64>(encoded_salt));
  if (salt == 0) {
    return Status::Error("persisted per-install selection salt must be non-zero");
  }
  return salt;
}

uint64 mint_per_install_selection_salt() {
  uint64 salt = 0;
  while (salt == 0) {
    salt = Random::secure_uint64();
  }
  return salt;
}

void persist_per_install_selection_salt_locked(const std::shared_ptr<KeyValueSyncInterface> &store, uint64 salt) {
  if (store == nullptr) {
    return;
  }
  if (salt == 0) {
    store->erase(kPerInstallSelectionSaltStoreKey.str());
    return;
  }
  store->set(kPerInstallSelectionSaltStoreKey.str(), to_string(salt));
}

void initialize_per_install_selection_salt_locked(const std::shared_ptr<KeyValueSyncInterface> &store) {
  if (store == nullptr) {
    return;
  }

  auto encoded_salt = store->get(kPerInstallSelectionSaltStoreKey.str());
  auto persisted_salt = parse_persisted_per_install_selection_salt(encoded_salt);
  if (persisted_salt.is_ok()) {
    per_install_selection_salt_cache().store(persisted_salt.ok(), std::memory_order_relaxed);
    return;
  }

  auto minted_salt = mint_per_install_selection_salt();
  per_install_selection_salt_cache().store(minted_salt, std::memory_order_relaxed);
  persist_per_install_selection_salt_locked(store, minted_salt);
}

// Per-installation salt mixed into stable_selection_hash so two installations
// sharing the same destination, platform, and time bucket do not deterministically
// pick the same profile (population-correlation defence against DPI). 0 is the
// "unset / no entropy" sentinel and reproduces the legacy deterministic vector.
//
// When a runtime KV store is configured, the salt is minted once, persisted
// there, and restored automatically on later launches. A host may also inject an
// externally persisted value via set_per_install_selection_salt(). A salt that
// changed each start would rotate the chosen profile across restarts and itself
// become a fingerprint, so the value must stay stable per installation. Tests
// still rely on the explicit 0 sentinel to reproduce the legacy deterministic
// vector.
uint64 effective_per_install_selection_salt() {
  return per_install_selection_salt_cache().load(std::memory_order_relaxed);
}

uint32 stable_selection_hash(const SelectionKey &key, const RuntimePlatformHints &platform) {
  string material = key.destination;
  material += '|';
  material += to_string(key.time_bucket);
  material += '|';
  material += to_string(static_cast<int>(platform.device_class));
  material += '|';
  material += to_string(static_cast<int>(platform.mobile_os));
  material += '|';
  material += to_string(static_cast<int>(platform.desktop_os));
  auto salt = effective_per_install_selection_salt();
  if (salt != 0) {
    // Only appended when a salt is configured, so the salt-free material (and
    // therefore every existing deterministic selection vector) is unchanged.
    material += '|';
    material += to_string(salt);
  }
  return crc32(material);
}

bool transport_confidence_allows_profile(const StealthRuntimeParams &runtime_params, BrowserProfile profile) {
  if (runtime_params.transport_confidence != TransportConfidence::Unknown) {
    return true;
  }
  return profile_fixture_metadata(profile).transport_claim_level == TransportClaimLevel::TlsOnly;
}

// ---------------------------------------------------------------------------
// Adaptive runtime profile rotation (in-memory only, Phase 1).
// ---------------------------------------------------------------------------

// One in-memory quarantine entry for a single emitted wire variant. The map is
// keyed by normalized_destination + BrowserProfile + hello_uses_ech only, never
// by route labels, platform strings, status text, or secrets.
struct RuntimeProfileFailureEntry final {
  uint32 recent_failures{0};
  Timestamp quarantined_until;
};

struct RuntimeProfileRotationCounterStorage final {
  std::atomic<uint64> profile_quarantine_hit_total{0};
  std::atomic<uint64> profile_quarantine_all_blocked_total{0};
  std::atomic<uint64> profile_failure_recorded_total{0};
  std::atomic<uint64> profile_success_cleared_total{0};
};

std::mutex &profile_quarantine_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<string, RuntimeProfileFailureEntry> &profile_quarantine_map() {
  static std::unordered_map<string, RuntimeProfileFailureEntry> map;
  return map;
}

RuntimeProfileRotationCounterStorage &runtime_profile_rotation_counters() {
  static RuntimeProfileRotationCounterStorage counters;
  return counters;
}

// Quarantine key derived ONLY from normalized destination + profile id + final
// ECH-on-the-wire flag. The normalized destination collapses case-only and
// trailing-dot aliases so `Example.COM` and `example.com.` share one entry.
string profile_quarantine_key(Slice destination, BrowserProfile profile, bool hello_uses_ech) {
  string key = normalized_runtime_destination_key(destination);
  key += '|';
  key += to_string(static_cast<int>(profile));
  key += '|';
  key += hello_uses_ech ? '1' : '0';
  return key;
}

// The emitted hello uses ECH only when the selected profile permits ECH AND the
// route decision resolved to RFC 9180 outer. A profile with allows_ech=false
// emits a non-ECH hello even when the route would otherwise enable ECH, so the
// wire variant must be keyed on this resolved value, not on the route intent.
bool hello_uses_ech_for_profile(BrowserProfile profile, EchMode ech_mode) {
  return profile_spec(profile).allows_ech && ech_mode == EchMode::Rfc9180Outer;
}

// Reads (and opportunistically reaps) the quarantine entry for one wire variant.
// A variant is actively quarantined only while its TTL is in the future AND it has
// reached the failure threshold. Expired entries are erased on read.
bool is_profile_variant_quarantined_locked(Slice destination, BrowserProfile profile, bool hello_uses_ech,
                                           uint32 failure_threshold) {
  auto &map = profile_quarantine_map();
  auto it = map.find(profile_quarantine_key(destination, profile, hello_uses_ech));
  if (it == map.end()) {
    return false;
  }
  if (!it->second.quarantined_until || it->second.quarantined_until.is_in_past()) {
    map.erase(it);
    return false;
  }
  return it->second.recent_failures >= failure_threshold;
}

// Deterministic weighted pick over a non-empty profile pool using the same stable
// per-destination/time-bucket hash as the legacy selector, so selection stays
// sticky (anti-churn) while excluding quarantined variants. Requires total
// weight > 0; every pool member is pre-filtered to weight > 0 by the caller.
BrowserProfile weighted_pick(const std::vector<BrowserProfile> &profiles, const ProfileWeights &weights,
                             const SelectionKey &key, const RuntimePlatformHints &platform) {
  uint32 total_weight = 0;
  for (auto profile : profiles) {
    total_weight += profile_weight(weights, profile);
  }
  CHECK(total_weight > 0);
  auto roll = stable_selection_hash(key, platform) % total_weight;
  uint32 cumulative_weight = 0;
  for (auto profile : profiles) {
    cumulative_weight += profile_weight(weights, profile);
    if (roll < cumulative_weight) {
      return profile;
    }
  }
  return profiles.back();
}

// Shared core of legacy and adaptive selection. Returns the final selectable pool
// (post transport-confidence and post release-gating filtering) plus the weighted
// baseline pick over that pool. advisory_blocked_total bookkeeping is preserved
// byte-for-byte from the legacy pick_runtime_profile so the disabled path is
// behaviour-neutral.
struct RuntimeProfileResolution final {
  std::vector<BrowserProfile> selectable;
  BrowserProfile baseline{BrowserProfile::Chrome133};
  SelectionKey key;
};

RuntimeProfileResolution resolve_runtime_profile(const StealthRuntimeParams &runtime_params, Slice destination,
                                                 int32 unix_time, const RuntimePlatformHints &platform) {
  RuntimeProfileResolution resolution;
  auto allowed_profiles = allowed_profiles_for_platform(platform);
  resolution.key = make_profile_selection_key(destination, unix_time);
  const auto &weights = runtime_params.profile_weights;

  std::vector<BrowserProfile> confidence_allowed_profiles;
  confidence_allowed_profiles.reserve(allowed_profiles.size());
  for (auto profile : allowed_profiles) {
    if (!transport_confidence_allows_profile(runtime_params, profile)) {
      continue;
    }
    if (profile_weight(weights, profile) == 0) {
      continue;
    }
    confidence_allowed_profiles.push_back(profile);
  }

  if (confidence_allowed_profiles.empty()) {
    for (auto profile : allowed_profiles) {
      if (transport_confidence_allows_profile(runtime_params, profile)) {
        resolution.selectable = {profile};
        resolution.baseline = profile;
        return resolution;
      }
    }
    auto fallback = allowed_profiles.back();
    resolution.selectable = {fallback};
    resolution.baseline = fallback;
    return resolution;
  }

  auto baseline = weighted_pick(confidence_allowed_profiles, weights, resolution.key, platform);

  if (!runtime_params.release_mode_profile_gating) {
    resolution.baseline = baseline;
    resolution.selectable = std::move(confidence_allowed_profiles);
    return resolution;
  }

  bool blocked_advisory = !profile_fixture_metadata(baseline).release_gating;
  std::vector<BrowserProfile> release_profiles;
  release_profiles.reserve(confidence_allowed_profiles.size());
  for (auto profile : confidence_allowed_profiles) {
    if (!profile_fixture_metadata(profile).release_gating) {
      continue;
    }
    if (profile_weight(weights, profile) == 0) {
      continue;
    }
    release_profiles.push_back(profile);
  }

  if (release_profiles.empty()) {
    runtime_profile_selection_counters().advisory_blocked_total.fetch_add(1, std::memory_order_relaxed);
    for (auto profile : confidence_allowed_profiles) {
      if (profile_fixture_metadata(profile).release_gating) {
        resolution.selectable = {profile};
        resolution.baseline = profile;
        return resolution;
      }
    }
    resolution.selectable = {baseline};
    resolution.baseline = baseline;
    return resolution;
  }

  if (blocked_advisory) {
    runtime_profile_selection_counters().advisory_blocked_total.fetch_add(1, std::memory_order_relaxed);
  }

  resolution.baseline = weighted_pick(release_profiles, weights, resolution.key, platform);
  resolution.selectable = std::move(release_profiles);
  return resolution;
}

}  // namespace tls_hello_profile_registry_internal

RuntimePlatformHints default_runtime_platform_hints() noexcept {
  return get_runtime_stealth_params_snapshot().platform_hints;
}

SelectionKey make_profile_selection_key(Slice destination, int32 unix_time) {
  SelectionKey key;
  key.destination = tls_hello_profile_registry_internal::normalized_runtime_destination_key(destination);

  auto unix_time64 = static_cast<int64>(unix_time);
  if (unix_time64 < 0) {
    unix_time64 = 0;
  }

  auto runtime_params = get_runtime_stealth_params_snapshot();
  auto bucket_seconds = runtime_params.flow_behavior.sticky_domain_rotation_window_sec;
  if (bucket_seconds == 0) {
    bucket_seconds = 1;
  }
  key.time_bucket = static_cast<uint32>(unix_time64 / bucket_seconds);
  return key;
}

void set_runtime_ech_failure_store(std::shared_ptr<KeyValueSyncInterface> store) {
  auto lock = std::scoped_lock(tls_hello_profile_registry_internal::route_failure_cache_mutex());
  tls_hello_profile_registry_internal::route_failure_store() = std::move(store);
  tls_hello_profile_registry_internal::route_failure_cache().clear();
  tls_hello_profile_registry_internal::initialize_per_install_selection_salt_locked(
      tls_hello_profile_registry_internal::route_failure_store());
}

void set_per_install_selection_salt(uint64 salt) noexcept {
  tls_hello_profile_registry_internal::per_install_selection_salt_cache().store(salt, std::memory_order_relaxed);
  auto lock = std::scoped_lock(tls_hello_profile_registry_internal::route_failure_cache_mutex());
  tls_hello_profile_registry_internal::persist_per_install_selection_salt_locked(
      tls_hello_profile_registry_internal::route_failure_store(), salt);
}

uint64 get_per_install_selection_salt() noexcept {
  return tls_hello_profile_registry_internal::per_install_selection_salt_cache().load(std::memory_order_relaxed);
}

void reset_per_install_selection_salt_for_tests() noexcept {
  tls_hello_profile_registry_internal::per_install_selection_salt_cache().store(0, std::memory_order_relaxed);
}

void reconcile_runtime_ech_failure_ttl(double ttl_seconds) {
  auto lock = std::scoped_lock(tls_hello_profile_registry_internal::route_failure_cache_mutex());
  auto max_disabled_until = Timestamp::in(ttl_seconds);

  auto &cache = tls_hello_profile_registry_internal::route_failure_cache();
  for (auto &it : cache) {
    if (tls_hello_profile_registry_internal::clamp_route_failure_disabled_until(it.second, max_disabled_until)) {
      tls_hello_profile_registry_internal::persist_route_failure_cache_entry_locked(it.first, 0, it.second);
    }
  }

  auto store = tls_hello_profile_registry_internal::route_failure_store();
  if (store == nullptr) {
    return;
  }

  auto persisted_entries = store->prefix_get(tls_hello_profile_registry_internal::kRuntimeEchStoreKeyPrefix);
  for (const auto &it : persisted_entries) {
    tls_hello_profile_registry_internal::RouteFailureCacheEntry entry;
    if (!tls_hello_profile_registry_internal::parse_route_failure_cache_entry(it.second, entry)) {
      continue;
    }
    if (!tls_hello_profile_registry_internal::clamp_route_failure_disabled_until(entry, max_disabled_until)) {
      continue;
    }
    store->set(it.first, tls_hello_profile_registry_internal::serialize_route_failure_cache_entry(entry));
  }
}

void note_runtime_ech_decision(const RuntimeEchDecision &decision, bool ech_enabled) noexcept {
  auto &counters = tls_hello_profile_registry_internal::runtime_ech_counters();
  if (ech_enabled) {
    counters.enabled_total.fetch_add(1, std::memory_order_relaxed);
    if (decision.reenabled_after_ttl) {
      counters.reenabled_total.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }
  if (decision.disabled_by_circuit_breaker) {
    counters.disabled_cb_total.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (decision.disabled_by_route) {
    counters.disabled_route_total.fetch_add(1, std::memory_order_relaxed);
  }
}

void note_runtime_ech_failure(Slice destination, int32 unix_time) {
  if (!tls_hello_profile_registry_internal::has_runtime_destination_identity(destination)) {
    return;
  }
  auto runtime_params = get_runtime_stealth_params_snapshot();
  auto lock = std::scoped_lock(tls_hello_profile_registry_internal::route_failure_cache_mutex());
  auto &entry = tls_hello_profile_registry_internal::route_failure_cache()
      [tls_hello_profile_registry_internal::route_failure_cache_key(destination, unix_time)];
  if (entry.disabled_until && entry.disabled_until.is_in_past()) {
    entry = tls_hello_profile_registry_internal::RouteFailureCacheEntry{};
  }
  if (entry.state.recent_ech_failures < std::numeric_limits<uint32>::max()) {
    entry.state.recent_ech_failures++;
  }
  entry.disabled_until = Timestamp::in(runtime_params.route_failure.ech_disable_ttl_seconds);
  if (entry.state.recent_ech_failures >= runtime_params.route_failure.ech_failure_threshold) {
    entry.state.ech_block_suspected = true;
    entry.disabled_until = Timestamp::in(runtime_params.route_failure.ech_disable_ttl_seconds);
  }
  tls_hello_profile_registry_internal::erase_legacy_route_failure_cache_entries_locked(destination, unix_time);
  tls_hello_profile_registry_internal::persist_route_failure_cache_entry_locked(destination, unix_time, entry);
}

void note_runtime_ech_success(Slice destination, int32 unix_time) {
  if (!tls_hello_profile_registry_internal::has_runtime_destination_identity(destination)) {
    return;
  }
  auto lock = std::scoped_lock(tls_hello_profile_registry_internal::route_failure_cache_mutex());
  tls_hello_profile_registry_internal::route_failure_cache().erase(
      tls_hello_profile_registry_internal::route_failure_cache_key(destination, unix_time));
  tls_hello_profile_registry_internal::erase_route_failure_cache_entry_locked(destination, unix_time);
  tls_hello_profile_registry_internal::erase_legacy_route_failure_cache_entries_locked(destination, unix_time);
  auto casefold_candidates =
      tls_hello_profile_registry_internal::collect_casefold_store_lookup_candidates_locked(destination, unix_time);
  tls_hello_profile_registry_internal::erase_casefold_store_lookup_candidates_locked(casefold_candidates);
}

void reset_runtime_ech_failure_state_for_tests() {
  auto lock = std::scoped_lock(tls_hello_profile_registry_internal::route_failure_cache_mutex());
  tls_hello_profile_registry_internal::route_failure_cache().clear();
}

RuntimeEchCounters get_runtime_ech_counters() noexcept {
  auto &counters = tls_hello_profile_registry_internal::runtime_ech_counters();
  RuntimeEchCounters result;
  result.enabled_total = counters.enabled_total.load(std::memory_order_relaxed);
  result.disabled_route_total = counters.disabled_route_total.load(std::memory_order_relaxed);
  result.disabled_cb_total = counters.disabled_cb_total.load(std::memory_order_relaxed);
  result.reenabled_total = counters.reenabled_total.load(std::memory_order_relaxed);
  return result;
}

void reset_runtime_ech_counters_for_tests() noexcept {
  auto &counters = tls_hello_profile_registry_internal::runtime_ech_counters();
  counters.enabled_total.store(0, std::memory_order_relaxed);
  counters.disabled_route_total.store(0, std::memory_order_relaxed);
  counters.disabled_cb_total.store(0, std::memory_order_relaxed);
  counters.reenabled_total.store(0, std::memory_order_relaxed);
}

RuntimeProfileSelectionCounters get_runtime_profile_selection_counters() noexcept {
  auto &counters = tls_hello_profile_registry_internal::runtime_profile_selection_counters();
  RuntimeProfileSelectionCounters result;
  result.advisory_blocked_total = counters.advisory_blocked_total.load(std::memory_order_relaxed);
  return result;
}

void reset_runtime_profile_selection_counters_for_tests() noexcept {
  auto &counters = tls_hello_profile_registry_internal::runtime_profile_selection_counters();
  counters.advisory_blocked_total.store(0, std::memory_order_relaxed);
}

Span<BrowserProfile> all_profiles() {
  return Span<BrowserProfile>(tls_hello_profile_registry_internal::ALL_PROFILES);
}

Span<BrowserProfile> allowed_profiles_for_platform(const RuntimePlatformHints &platform) {
  if (platform.device_class == DeviceClass::Mobile) {
    if (platform.mobile_os == MobileOs::IOS) {
      return Span<BrowserProfile>(tls_hello_profile_registry_internal::IOS_MOBILE_PROFILES);
    }
    if (platform.mobile_os == MobileOs::Android) {
      return Span<BrowserProfile>(tls_hello_profile_registry_internal::ANDROID_MOBILE_PROFILES);
    }
    return Span<BrowserProfile>(tls_hello_profile_registry_internal::MOBILE_PROFILES);
  }
  if (platform.desktop_os == DesktopOs::Darwin) {
    return Span<BrowserProfile>(tls_hello_profile_registry_internal::DARWIN_DESKTOP_PROFILES);
  }
  if (platform.desktop_os == DesktopOs::Windows) {
    return Span<BrowserProfile>(tls_hello_profile_registry_internal::WINDOWS_DESKTOP_PROFILES);
  }
  return Span<BrowserProfile>(tls_hello_profile_registry_internal::NON_DARWIN_DESKTOP_PROFILES);
}

const ProfileSpec &profile_spec(BrowserProfile profile) {
  return tls_hello_profile_registry_internal::PROFILE_SPECS[tls_hello_profile_registry_internal::profile_index(
      profile)];
}

const ProfileFixtureMetadata &profile_fixture_metadata(BrowserProfile profile) {
  return tls_hello_profile_registry_internal::PROFILE_FIXTURES[tls_hello_profile_registry_internal::profile_index(
      profile)];
}

ProfileWeights default_profile_weights() {
  return get_runtime_stealth_params_snapshot().profile_weights;
}

BrowserProfile pick_profile_sticky(const ProfileWeights &weights, const SelectionKey &key,
                                   const RuntimePlatformHints &platform, Span<BrowserProfile> allowed_profiles,
                                   IRng &rng) {
  (void)rng;
  CHECK(!allowed_profiles.empty());

  uint32 total_weight = 0;
  for (auto profile : allowed_profiles) {
    total_weight += tls_hello_profile_registry_internal::profile_weight(weights, profile);
  }
  CHECK(total_weight > 0);

  auto roll = tls_hello_profile_registry_internal::stable_selection_hash(key, platform) % total_weight;
  uint32 cumulative_weight = 0;
  for (auto profile : allowed_profiles) {
    cumulative_weight += tls_hello_profile_registry_internal::profile_weight(weights, profile);
    if (roll < cumulative_weight) {
      return profile;
    }
  }

  return allowed_profiles.back();
}

BrowserProfile pick_runtime_profile(Slice destination, int32 unix_time, const RuntimePlatformHints &platform) {
  // Legacy stable wrapper: one weighted pick over the confidence/release-filtered
  // pool, no quarantine. Adaptive rotation is a strict superset reachable via
  // pick_runtime_profile_adaptive when the rotation policy is enabled.
  auto runtime_params = get_runtime_stealth_params_snapshot();
  return tls_hello_profile_registry_internal::resolve_runtime_profile(runtime_params, destination, unix_time, platform)
      .baseline;
}

RuntimeProfileSelectionDecision pick_runtime_profile_adaptive(Slice destination, int32 unix_time,
                                                              const RuntimePlatformHints &platform, EchMode ech_mode) {
  using namespace tls_hello_profile_registry_internal;
  auto runtime_params = get_runtime_stealth_params_snapshot();
  auto resolution = resolve_runtime_profile(runtime_params, destination, unix_time, platform);

  RuntimeProfileSelectionDecision decision;
  decision.profile = resolution.baseline;
  decision.hello_uses_ech = hello_uses_ech_for_profile(resolution.baseline, ech_mode);

  // Rotation is opt-in; an empty/degenerate destination has no stable quarantine
  // identity. In both cases the decision is the legacy baseline, leaving the
  // disabled path behaviour-neutral. A single-element pool still runs the logic
  // below so that quarantining the only allowed lane records the all-blocked
  // fail-closed accounting instead of silently returning a quarantined variant.
  if (!runtime_params.profile_rotation.enabled || !has_runtime_destination_identity(destination)) {
    return decision;
  }

  auto failure_threshold = runtime_params.profile_rotation.failure_threshold;
  auto lock = std::scoped_lock(profile_quarantine_mutex());

  uint32 quarantined_count = 0;
  for (auto profile : resolution.selectable) {
    if (is_profile_variant_quarantined_locked(destination, profile, hello_uses_ech_for_profile(profile, ech_mode),
                                              failure_threshold)) {
      quarantined_count++;
    }
  }
  decision.quarantined_candidate_count = quarantined_count;

  if (!is_profile_variant_quarantined_locked(destination, resolution.baseline, decision.hello_uses_ech,
                                             failure_threshold)) {
    return decision;
  }

  std::vector<BrowserProfile> available;
  available.reserve(resolution.selectable.size());
  for (auto profile : resolution.selectable) {
    if (!is_profile_variant_quarantined_locked(destination, profile, hello_uses_ech_for_profile(profile, ech_mode),
                                               failure_threshold)) {
      available.push_back(profile);
    }
  }

  if (available.empty()) {
    // Every allowed wire variant is quarantined: fail closed by keeping the
    // baseline. Rotation never widens beyond the already-allowed platform set.
    runtime_profile_rotation_counters().profile_quarantine_all_blocked_total.fetch_add(1, std::memory_order_relaxed);
    return decision;
  }

  decision.profile = weighted_pick(available, runtime_params.profile_weights, resolution.key, platform);
  decision.hello_uses_ech = hello_uses_ech_for_profile(decision.profile, ech_mode);
  decision.avoided_quarantined_profile = true;
  runtime_profile_rotation_counters().profile_quarantine_hit_total.fetch_add(1, std::memory_order_relaxed);
  return decision;
}

bool runtime_profile_failure_signal_is_quarantine_eligible(RuntimeProfileFailureSignal signal) noexcept {
  switch (signal) {
    case RuntimeProfileFailureSignal::MalformedHelloResponse:
    case RuntimeProfileFailureSignal::TransportRejectionAfterHello:
      return true;
    case RuntimeProfileFailureSignal::None:
    case RuntimeProfileFailureSignal::WrongRegime:
    case RuntimeProfileFailureSignal::ResponseHashMismatch:
      return false;
  }
  // Garbage / out-of-enum signal: fail closed, never quarantine.
  return false;
}

void note_runtime_profile_failure(Slice destination, const RuntimeProfileWireVariant &variant,
                                  RuntimeProfileFailureSignal signal) {
  using namespace tls_hello_profile_registry_internal;
  // Conservative attribution: only wire-shape rejections quarantine a profile.
  // Wrong-regime / response-hash-mismatch / pre-hello signals are no-ops and must
  // not even touch counters (false-positive resistance + fail-closed fuzz input).
  if (!runtime_profile_failure_signal_is_quarantine_eligible(signal)) {
    return;
  }
  if (!has_runtime_destination_identity(destination)) {
    return;
  }
  auto runtime_params = get_runtime_stealth_params_snapshot();
  if (!runtime_params.profile_rotation.enabled) {
    return;
  }

  auto lock = std::scoped_lock(profile_quarantine_mutex());
  auto &entry =
      profile_quarantine_map()[profile_quarantine_key(destination, variant.profile, variant.hello_uses_ech)];
  if (entry.quarantined_until && entry.quarantined_until.is_in_past()) {
    entry = RuntimeProfileFailureEntry{};
  }
  if (entry.recent_failures < std::numeric_limits<uint32>::max()) {
    entry.recent_failures++;
  }
  entry.quarantined_until = Timestamp::in(runtime_params.profile_rotation.quarantine_ttl_seconds);
  runtime_profile_rotation_counters().profile_failure_recorded_total.fetch_add(1, std::memory_order_relaxed);
}

void note_runtime_profile_success(Slice destination, const RuntimeProfileWireVariant &variant) {
  using namespace tls_hello_profile_registry_internal;
  if (!has_runtime_destination_identity(destination)) {
    return;
  }
  auto lock = std::scoped_lock(profile_quarantine_mutex());
  auto erased =
      profile_quarantine_map().erase(profile_quarantine_key(destination, variant.profile, variant.hello_uses_ech));
  if (erased != 0) {
    runtime_profile_rotation_counters().profile_success_cleared_total.fetch_add(1, std::memory_order_relaxed);
  }
}

RuntimeProfileRotationCounters get_runtime_profile_rotation_counters() noexcept {
  auto &counters = tls_hello_profile_registry_internal::runtime_profile_rotation_counters();
  RuntimeProfileRotationCounters result;
  result.profile_quarantine_hit_total = counters.profile_quarantine_hit_total.load(std::memory_order_relaxed);
  result.profile_quarantine_all_blocked_total =
      counters.profile_quarantine_all_blocked_total.load(std::memory_order_relaxed);
  result.profile_failure_recorded_total = counters.profile_failure_recorded_total.load(std::memory_order_relaxed);
  result.profile_success_cleared_total = counters.profile_success_cleared_total.load(std::memory_order_relaxed);
  return result;
}

void reset_runtime_profile_rotation_counters_for_tests() noexcept {
  auto &counters = tls_hello_profile_registry_internal::runtime_profile_rotation_counters();
  counters.profile_quarantine_hit_total.store(0, std::memory_order_relaxed);
  counters.profile_quarantine_all_blocked_total.store(0, std::memory_order_relaxed);
  counters.profile_failure_recorded_total.store(0, std::memory_order_relaxed);
  counters.profile_success_cleared_total.store(0, std::memory_order_relaxed);
}

void reset_runtime_profile_quarantine_state_for_tests() {
  auto lock = std::scoped_lock(tls_hello_profile_registry_internal::profile_quarantine_mutex());
  tls_hello_profile_registry_internal::profile_quarantine_map().clear();
}

EchMode ech_mode_for_route(const NetworkRouteHints &route, const RouteFailureState &state) noexcept {
  auto runtime_params = get_runtime_stealth_params_snapshot();
  auto active_policy = tls_hello_profile_registry_internal::active_policy_for_route(runtime_params, route);
  auto &route_policy =
      tls_hello_profile_registry_internal::route_policy_entry_for_active_policy(runtime_params, active_policy);
  if (active_policy == RuntimeActivePolicy::Unknown || active_policy == RuntimeActivePolicy::RuEgress) {
    return route_policy.ech_mode;
  }
  if (state.ech_block_suspected || state.recent_ech_failures >= runtime_params.route_failure.ech_failure_threshold) {
    return EchMode::Disabled;
  }
  return route_policy.ech_mode;
}

RuntimeEchDecision get_runtime_ech_decision(Slice destination, int32 unix_time,
                                            const NetworkRouteHints &route) noexcept {
  auto runtime_params = get_runtime_stealth_params_snapshot();
  auto active_policy = tls_hello_profile_registry_internal::active_policy_for_route(runtime_params, route);
  auto &route_policy =
      tls_hello_profile_registry_internal::route_policy_entry_for_active_policy(runtime_params, active_policy);
  RuntimeEchDecision decision;
  if (active_policy == RuntimeActivePolicy::Unknown) {
    decision.ech_mode = route_policy.ech_mode;
    decision.disabled_by_route = true;
    return decision;
  }
  if (active_policy == RuntimeActivePolicy::RuEgress) {
    decision.ech_mode = route_policy.ech_mode;
    decision.disabled_by_route = true;
    return decision;
  }
  if (route_policy.ech_mode == EchMode::Disabled) {
    decision.ech_mode = EchMode::Disabled;
    decision.disabled_by_route = true;
    return decision;
  }
  if (!tls_hello_profile_registry_internal::has_runtime_destination_identity(destination)) {
    // Empty/degenerate destinations are invalid for stable per-destination
    // route-failure accounting; fail closed without touching persistent state.
    decision.ech_mode = EchMode::Disabled;
    decision.disabled_by_route = true;
    return decision;
  }

  auto lock = std::scoped_lock(tls_hello_profile_registry_internal::route_failure_cache_mutex());
  auto max_disabled_until = Timestamp::in(runtime_params.route_failure.ech_disable_ttl_seconds);
  bool saw_expired_blocked_entry = false;
  auto state = tls_hello_profile_registry_internal::get_runtime_route_failure_state_locked(
      destination, unix_time, &saw_expired_blocked_entry, max_disabled_until);
  auto active_circuit_breaker =
      state.ech_block_suspected || state.recent_ech_failures >= runtime_params.route_failure.ech_failure_threshold;
  decision.reenabled_after_ttl = saw_expired_blocked_entry && !active_circuit_breaker;
  decision.ech_mode = ech_mode_for_route(route, state);
  decision.disabled_by_circuit_breaker = decision.ech_mode == EchMode::Disabled && active_circuit_breaker;
  return decision;
}

EchMode runtime_ech_mode_for_route(Slice destination, int32 unix_time, const NetworkRouteHints &route) noexcept {
  return get_runtime_ech_decision(destination, unix_time, route).ech_mode;
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
