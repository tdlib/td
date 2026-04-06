//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "tddb/td/db/KeyValueSyncInterface.h"

#include "td/utils/tests.h"
#include "td/utils/Time.h"

namespace {

using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::get_runtime_ech_counters;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::note_runtime_ech_failure;
using td::mtproto::stealth::note_runtime_ech_success;
using td::mtproto::stealth::reset_runtime_ech_counters_for_tests;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::runtime_ech_mode_for_route;
using td::mtproto::stealth::set_runtime_ech_failure_store;

class MemoryKeyValue final : public td::KeyValueSyncInterface {
 public:
  SeqNo set(td::string key, td::string value) final {
    map_[std::move(key)] = std::move(value);
    return ++seq_no_;
  }

  bool isset(const td::string &key) final {
    return map_.count(key) != 0;
  }

  td::string get(const td::string &key) final {
    auto it = map_.find(key);
    return it == map_.end() ? td::string() : it->second;
  }

  void for_each(std::function<void(td::Slice, td::Slice)> func) final {
    for (const auto &it : map_) {
      func(it.first, it.second);
    }
  }

  std::unordered_map<td::string, td::string, td::Hash<td::string>> prefix_get(td::Slice prefix) final {
    std::unordered_map<td::string, td::string, td::Hash<td::string>> result;
    for (const auto &it : map_) {
      if (prefix.size() <= it.first.size() && td::Slice(it.first).substr(0, prefix.size()) == prefix) {
        result.emplace(it.first, it.second);
      }
    }
    return result;
  }

  td::FlatHashMap<td::string, td::string> get_all() final {
    td::FlatHashMap<td::string, td::string> result;
    for (const auto &it : map_) {
      result.emplace(it.first, it.second);
    }
    return result;
  }

  SeqNo erase(const td::string &key) final {
    map_.erase(key);
    return ++seq_no_;
  }

  SeqNo erase_batch(td::vector<td::string> keys) final {
    for (const auto &key : keys) {
      map_.erase(key);
    }
    return ++seq_no_;
  }

  void erase_by_prefix(td::Slice prefix) final {
    td::vector<td::string> keys;
    for (const auto &it : map_) {
      if (prefix.size() <= it.first.size() && td::Slice(it.first).substr(0, prefix.size()) == prefix) {
        keys.push_back(it.first);
      }
    }
    for (const auto &key : keys) {
      map_.erase(key);
    }
  }

  void force_sync(td::Promise<> &&promise, const char *source) final {
    (void)source;
    promise.set_value(td::Unit());
  }

  void close(td::Promise<> promise) final {
    promise.set_value(td::Unit());
  }

 private:
  SeqNo seq_no_{0};
  std::unordered_map<td::string, td::string, td::Hash<td::string>> map_;
};

class ScopedRuntimeEchStore final {
 public:
  explicit ScopedRuntimeEchStore(std::shared_ptr<td::KeyValueSyncInterface> store) {
    set_runtime_ech_failure_store(std::move(store));
  }

  ~ScopedRuntimeEchStore() {
    set_runtime_ech_failure_store(nullptr);
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
  }
};

TEST(TlsRouteFailureCache, ReachingFailureThresholdDisablesEchForDestinationBucket) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();

  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;

  const td::Slice destination("www.google.com");
  const td::int32 unix_time = 1712345678;

  ASSERT_TRUE(EchMode::Rfc9180Outer == runtime_ech_mode_for_route(destination, unix_time, route_hints));
  note_runtime_ech_failure(destination, unix_time);
  ASSERT_TRUE(EchMode::Rfc9180Outer == runtime_ech_mode_for_route(destination, unix_time, route_hints));
  note_runtime_ech_failure(destination, unix_time);
  ASSERT_TRUE(EchMode::Rfc9180Outer == runtime_ech_mode_for_route(destination, unix_time, route_hints));
  note_runtime_ech_failure(destination, unix_time);
  ASSERT_TRUE(EchMode::Disabled == runtime_ech_mode_for_route(destination, unix_time, route_hints));
}

TEST(TlsRouteFailureCache, SuccessClearsCircuitBreakerState) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();

  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;

  const td::Slice destination("www.google.com");
  const td::int32 unix_time = 1712345678;

  note_runtime_ech_failure(destination, unix_time);
  note_runtime_ech_failure(destination, unix_time);
  note_runtime_ech_failure(destination, unix_time);
  ASSERT_TRUE(EchMode::Disabled == runtime_ech_mode_for_route(destination, unix_time, route_hints));

  note_runtime_ech_success(destination, unix_time);
  ASSERT_TRUE(EchMode::Rfc9180Outer == runtime_ech_mode_for_route(destination, unix_time, route_hints));
}

TEST(TlsRouteFailureCache, DisableStateExpiresAfterTtl) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();

  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;

  const td::Slice destination("www.google.com");
  const td::int32 unix_time = 1712345678;

  note_runtime_ech_failure(destination, unix_time);
  note_runtime_ech_failure(destination, unix_time);
  note_runtime_ech_failure(destination, unix_time);
  ASSERT_TRUE(EchMode::Disabled == runtime_ech_mode_for_route(destination, unix_time, route_hints));

  td::Time::jump_in_future(td::Time::now() + 301.0);
  ASSERT_TRUE(EchMode::Rfc9180Outer == runtime_ech_mode_for_route(destination, unix_time, route_hints));
}

TEST(TlsRouteFailureCache, RouteGuardsStillFailClosedWithoutHealthyNonRuLane) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();

  const td::Slice destination("www.google.com");
  const td::int32 unix_time = 1712345678;

  NetworkRouteHints unknown_route;
  unknown_route.is_known = false;
  unknown_route.is_ru = false;
  ASSERT_TRUE(EchMode::Disabled == runtime_ech_mode_for_route(destination, unix_time, unknown_route));

  NetworkRouteHints ru_route;
  ru_route.is_known = true;
  ru_route.is_ru = true;
  ASSERT_TRUE(EchMode::Disabled == runtime_ech_mode_for_route(destination, unix_time, ru_route));
}

TEST(TlsRouteFailureCache, PersistentStoreReloadsCircuitBreakerStateAfterMemoryReset) {
  auto store = std::make_shared<MemoryKeyValue>();
  ScopedRuntimeEchStore scoped_store(store);

  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();

  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;

  const td::Slice destination("persist.example.com");
  const td::int32 unix_time = 1712345678;

  note_runtime_ech_failure(destination, unix_time);
  note_runtime_ech_failure(destination, unix_time);
  note_runtime_ech_failure(destination, unix_time);
  ASSERT_EQ(1u, store->get_all().size());
  ASSERT_TRUE(EchMode::Disabled == runtime_ech_mode_for_route(destination, unix_time, route_hints));

  reset_runtime_ech_failure_state_for_tests();
  ASSERT_EQ(1u, store->get_all().size());
  ASSERT_TRUE(EchMode::Disabled == runtime_ech_mode_for_route(destination, unix_time, route_hints));
}

}  // namespace