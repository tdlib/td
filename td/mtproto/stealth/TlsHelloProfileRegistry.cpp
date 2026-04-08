// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "tddb/td/db/KeyValueSyncInterface.h"

#include "td/utils/crypto.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/Time.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

namespace td {
namespace mtproto {
namespace stealth {
namespace {

constexpr BrowserProfile ALL_PROFILES[] = {
    BrowserProfile::Chrome133,  BrowserProfile::Chrome131, BrowserProfile::Chrome120,        BrowserProfile::Firefox148,
    BrowserProfile::Safari26_3, BrowserProfile::IOS14,     BrowserProfile::Android11_OkHttp,
};

constexpr BrowserProfile DARWIN_DESKTOP_PROFILES[] = {
    BrowserProfile::Chrome133,  BrowserProfile::Chrome131,  BrowserProfile::Chrome120,
    BrowserProfile::Safari26_3, BrowserProfile::Firefox148,
};

constexpr BrowserProfile NON_DARWIN_DESKTOP_PROFILES[] = {
    BrowserProfile::Chrome133,
    BrowserProfile::Chrome131,
    BrowserProfile::Chrome120,
    BrowserProfile::Firefox148,
};

constexpr BrowserProfile MOBILE_PROFILES[] = {
    BrowserProfile::IOS14,
    BrowserProfile::Android11_OkHttp,
};

constexpr BrowserProfile IOS_MOBILE_PROFILES[] = {
    BrowserProfile::IOS14,
};

constexpr BrowserProfile ANDROID_MOBILE_PROFILES[] = {
    BrowserProfile::Android11_OkHttp,
};

constexpr ProfileSpec PROFILE_SPECS[] = {
    {BrowserProfile::Chrome133, Slice("chrome133"), 0x44CD, 0, true, true, true, true, 0x11EC,
     ExtensionOrderPolicy::ChromeShuffleAnchored},
    {BrowserProfile::Chrome131, Slice("chrome131"), 0x4469, 0, true, true, true, true, 0x11EC,
     ExtensionOrderPolicy::ChromeShuffleAnchored},
    {BrowserProfile::Chrome120, Slice("chrome120"), 0x4469, 0, true, true, true, false, 0,
     ExtensionOrderPolicy::ChromeShuffleAnchored},
    {BrowserProfile::Firefox148, Slice("firefox148"), 0, 0x4001, true, false, true, true, 0x11EC,
     ExtensionOrderPolicy::FixedFromFixture},
    {BrowserProfile::Safari26_3, Slice("safari26_3"), 0, 0, false, false, true, false, 0,
     ExtensionOrderPolicy::FixedFromFixture},
    {BrowserProfile::IOS14, Slice("ios14"), 0, 0, false, false, true, false, 0, ExtensionOrderPolicy::FixedFromFixture},
    {BrowserProfile::Android11_OkHttp, Slice("android11_okhttp"), 0, 0, false, false, true, false, 0,
     ExtensionOrderPolicy::FixedFromFixture},
};

constexpr ProfileFixtureMetadata PROFILE_FIXTURES[] = {
    {Slice("curl_cffi:chrome133"), ProfileFixtureSourceKind::CurlCffiCapture, ProfileTrustTier::Verified, true, true,
     true},
    {Slice("curl_cffi:chrome131"), ProfileFixtureSourceKind::CurlCffiCapture, ProfileTrustTier::Verified, true, true,
     true},
    {Slice("browser_capture:chrome120_non_pq"), ProfileFixtureSourceKind::BrowserCapture, ProfileTrustTier::Verified,
     true, true, true},
    {Slice("browser_capture:firefox148"), ProfileFixtureSourceKind::BrowserCapture, ProfileTrustTier::Verified, true,
     true, true},
    {Slice("utls:HelloSafari_26_3"), ProfileFixtureSourceKind::UtlsSnapshot, ProfileTrustTier::Advisory, false, false,
     false},
    {Slice("utls:HelloIOS_14"), ProfileFixtureSourceKind::UtlsSnapshot, ProfileTrustTier::Advisory, false, false,
     false},
    {Slice("utls:HelloAndroid_11_OkHttp"), ProfileFixtureSourceKind::UtlsSnapshot, ProfileTrustTier::Advisory, false,
     false, false},
};

constexpr Slice kRuntimeEchStoreKeyPrefix("stealth_ech_cb#");
constexpr uint32 kRouteFailureKeyBucketSeconds = 86400;

struct RouteFailureCacheEntry final {
  RouteFailureState state;
  Timestamp disabled_until;
};

struct RuntimeEchCounterStorage final {
  std::atomic<uint64> enabled_total{0};
  std::atomic<uint64> disabled_route_total{0};
  std::atomic<uint64> disabled_cb_total{0};
  std::atomic<uint64> reenabled_total{0};
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
  return static_cast<size_t>(profile);
}

string route_failure_cache_key(Slice destination, int32 unix_time) {
  auto unix_time64 = static_cast<int64>(unix_time);
  if (unix_time64 < 0) {
    unix_time64 = 0;
  }

  string key = destination.str();
  key += '|';
  key += std::to_string(static_cast<uint32>(unix_time64 / kRouteFailureKeyBucketSeconds));
  return key;
}

string route_failure_store_key(Slice destination, int32 unix_time) {
  return kRuntimeEchStoreKeyPrefix.str() + route_failure_cache_key(destination, unix_time);
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
  char *end = nullptr;
  auto parsed = std::strtoll(text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
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
  store->erase(route_failure_store_key(destination, unix_time));
}

RouteFailureState get_runtime_route_failure_state_locked(Slice destination, int32 unix_time) {
  auto &cache = route_failure_cache();
  auto key = route_failure_cache_key(destination, unix_time);
  auto it = cache.find(key);
  if (it == cache.end()) {
    auto store = route_failure_store();
    if (store == nullptr) {
      return RouteFailureState{};
    }
    auto serialized = store->get(route_failure_store_key(destination, unix_time));
    if (serialized.empty()) {
      return RouteFailureState{};
    }
    RouteFailureCacheEntry entry;
    if (!parse_route_failure_cache_entry(serialized, entry)) {
      entry = make_fail_closed_route_failure_cache_entry();
      auto inserted = cache.emplace(key, entry);
      persist_route_failure_cache_entry_locked(destination, unix_time, inserted.first->second);
      return inserted.first->second.state;
    }
    if (!entry.disabled_until) {
      store->erase(route_failure_store_key(destination, unix_time));
      return RouteFailureState{};
    }
    if (entry.disabled_until && entry.disabled_until.is_in_past()) {
      store->erase(route_failure_store_key(destination, unix_time));
      return RouteFailureState{};
    }
    auto inserted = cache.emplace(key, std::move(entry));
    return inserted.first->second.state;
  }
  if (it->second.disabled_until && it->second.disabled_until.is_in_past()) {
    erase_route_failure_cache_entry_locked(destination, unix_time);
    cache.erase(it);
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
    case BrowserProfile::Firefox148:
      return weights.firefox148;
    case BrowserProfile::Safari26_3:
      return weights.safari26_3;
    case BrowserProfile::IOS14:
      return weights.ios14;
    case BrowserProfile::Android11_OkHttp:
      return weights.android11_okhttp;
    default:
      UNREACHABLE();
      return 0;
  }
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
  return crc32(material);
}

}  // namespace

RuntimePlatformHints default_runtime_platform_hints() noexcept {
  return get_runtime_stealth_params_snapshot().platform_hints;
}

SelectionKey make_profile_selection_key(Slice destination, int32 unix_time) {
  SelectionKey key;
  key.destination = destination.str();

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
  auto lock = std::lock_guard<std::mutex>(route_failure_cache_mutex());
  route_failure_store() = std::move(store);
  route_failure_cache().clear();
}

void note_runtime_ech_decision(const RuntimeEchDecision &decision, bool ech_enabled) noexcept {
  auto &counters = runtime_ech_counters();
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
  auto runtime_params = get_runtime_stealth_params_snapshot();
  auto lock = std::lock_guard<std::mutex>(route_failure_cache_mutex());
  auto &entry = route_failure_cache()[route_failure_cache_key(destination, unix_time)];
  if (entry.disabled_until && entry.disabled_until.is_in_past()) {
    entry = RouteFailureCacheEntry{};
  }
  entry.state.recent_ech_failures++;
  entry.disabled_until = Timestamp::in(runtime_params.route_failure.ech_disable_ttl_seconds);
  if (entry.state.recent_ech_failures >= runtime_params.route_failure.ech_failure_threshold) {
    entry.state.ech_block_suspected = true;
    entry.disabled_until = Timestamp::in(runtime_params.route_failure.ech_disable_ttl_seconds);
  }
  persist_route_failure_cache_entry_locked(destination, unix_time, entry);
}

void note_runtime_ech_success(Slice destination, int32 unix_time) {
  auto lock = std::lock_guard<std::mutex>(route_failure_cache_mutex());
  route_failure_cache().erase(route_failure_cache_key(destination, unix_time));
  erase_route_failure_cache_entry_locked(destination, unix_time);
}

void reset_runtime_ech_failure_state_for_tests() {
  auto lock = std::lock_guard<std::mutex>(route_failure_cache_mutex());
  route_failure_cache().clear();
}

RuntimeEchCounters get_runtime_ech_counters() noexcept {
  auto &counters = runtime_ech_counters();
  RuntimeEchCounters result;
  result.enabled_total = counters.enabled_total.load(std::memory_order_relaxed);
  result.disabled_route_total = counters.disabled_route_total.load(std::memory_order_relaxed);
  result.disabled_cb_total = counters.disabled_cb_total.load(std::memory_order_relaxed);
  result.reenabled_total = counters.reenabled_total.load(std::memory_order_relaxed);
  return result;
}

void reset_runtime_ech_counters_for_tests() noexcept {
  auto &counters = runtime_ech_counters();
  counters.enabled_total.store(0, std::memory_order_relaxed);
  counters.disabled_route_total.store(0, std::memory_order_relaxed);
  counters.disabled_cb_total.store(0, std::memory_order_relaxed);
  counters.reenabled_total.store(0, std::memory_order_relaxed);
}

Span<BrowserProfile> all_profiles() {
  return Span<BrowserProfile>(ALL_PROFILES);
}

Span<BrowserProfile> allowed_profiles_for_platform(const RuntimePlatformHints &platform) {
  if (platform.device_class == DeviceClass::Mobile) {
    if (platform.mobile_os == MobileOs::IOS) {
      return Span<BrowserProfile>(IOS_MOBILE_PROFILES);
    }
    if (platform.mobile_os == MobileOs::Android) {
      return Span<BrowserProfile>(ANDROID_MOBILE_PROFILES);
    }
    return Span<BrowserProfile>(MOBILE_PROFILES);
  }
  if (platform.desktop_os == DesktopOs::Darwin) {
    return Span<BrowserProfile>(DARWIN_DESKTOP_PROFILES);
  }
  return Span<BrowserProfile>(NON_DARWIN_DESKTOP_PROFILES);
}

const ProfileSpec &profile_spec(BrowserProfile profile) {
  return PROFILE_SPECS[profile_index(profile)];
}

const ProfileFixtureMetadata &profile_fixture_metadata(BrowserProfile profile) {
  return PROFILE_FIXTURES[profile_index(profile)];
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
    total_weight += profile_weight(weights, profile);
  }
  CHECK(total_weight > 0);

  auto roll = stable_selection_hash(key, platform) % total_weight;
  uint32 cumulative_weight = 0;
  for (auto profile : allowed_profiles) {
    cumulative_weight += profile_weight(weights, profile);
    if (roll < cumulative_weight) {
      return profile;
    }
  }

  return allowed_profiles.back();
}

BrowserProfile pick_runtime_profile(Slice destination, int32 unix_time, const RuntimePlatformHints &platform) {
  auto allowed_profiles = allowed_profiles_for_platform(platform);
  auto key = make_profile_selection_key(destination, unix_time);
  auto weights = default_profile_weights();

  uint32 total_weight = 0;
  for (auto profile : allowed_profiles) {
    total_weight += profile_weight(weights, profile);
  }
  CHECK(total_weight > 0);

  auto roll = stable_selection_hash(key, platform) % total_weight;
  uint32 cumulative_weight = 0;
  for (auto profile : allowed_profiles) {
    cumulative_weight += profile_weight(weights, profile);
    if (roll < cumulative_weight) {
      return profile;
    }
  }

  return allowed_profiles.back();
}

EchMode ech_mode_for_route(const NetworkRouteHints &route, const RouteFailureState &state) noexcept {
  auto runtime_params = get_runtime_stealth_params_snapshot();
  auto active_policy = active_policy_for_route(runtime_params, route);
  auto &route_policy = route_policy_entry_for_active_policy(runtime_params, active_policy);
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
  auto active_policy = active_policy_for_route(runtime_params, route);
  auto &route_policy = route_policy_entry_for_active_policy(runtime_params, active_policy);
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

  auto lock = std::lock_guard<std::mutex>(route_failure_cache_mutex());
  auto key = route_failure_cache_key(destination, unix_time);
  auto it = route_failure_cache().find(key);
  if (it != route_failure_cache().end() && it->second.disabled_until && it->second.disabled_until.is_in_past()) {
    decision.reenabled_after_ttl = it->second.state.ech_block_suspected;
    erase_route_failure_cache_entry_locked(destination, unix_time);
    route_failure_cache().erase(it);
  }

  auto state = get_runtime_route_failure_state_locked(destination, unix_time);
  decision.ech_mode = ech_mode_for_route(route, state);
  decision.disabled_by_circuit_breaker =
      decision.ech_mode == EchMode::Disabled &&
      (state.ech_block_suspected || state.recent_ech_failures >= runtime_params.route_failure.ech_failure_threshold);
  return decision;
}

EchMode runtime_ech_mode_for_route(Slice destination, int32 unix_time, const NetworkRouteHints &route) noexcept {
  return get_runtime_ech_decision(destination, unix_time, route).ech_mode;
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td