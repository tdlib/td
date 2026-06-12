// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/stealth/ech_route_failure_store_test_utils.h"

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace td::mtproto::test {
namespace {

using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::RuntimeEchDecision;

class CountingEchRouteFailureStore final : public KeyValueSyncInterface {
 public:
  SeqNo set(string key, string value) final {
    set_calls++;
    map_[std::move(key)] = std::move(value);
    return ++seq_no_;
  }

  bool isset(const string &key) final {
    return map_.count(key) != 0;
  }

  string get(const string &key) final {
    get_calls++;
    auto it = map_.find(key);
    return it == map_.end() ? string() : it->second;
  }

  void for_each(std::function<void(Slice, Slice)> func) final {
    for (const auto &it : map_) {
      func(it.first, it.second);
    }
  }

  std::unordered_map<string, string, Hash<string>> prefix_get(Slice prefix) final {
    prefix_get_calls++;
    std::unordered_map<string, string, Hash<string>> result;
    for (const auto &it : map_) {
      if (prefix.size() <= it.first.size() && Slice(it.first).substr(0, prefix.size()) == prefix) {
        result.emplace(it.first, it.second);
      }
    }
    return result;
  }

  FlatHashMap<string, string> get_all() final {
    FlatHashMap<string, string> result;
    for (const auto &it : map_) {
      result.emplace(it.first, it.second);
    }
    return result;
  }

  SeqNo erase(const string &key) final {
    erase_calls++;
    map_.erase(key);
    return ++seq_no_;
  }

  SeqNo erase_batch(vector<string> keys) final {
    erase_batch_calls++;
    for (const auto &key : keys) {
      map_.erase(key);
    }
    return ++seq_no_;
  }

  void erase_by_prefix(Slice prefix) final {
    vector<string> keys;
    for (const auto &it : map_) {
      if (prefix.size() <= it.first.size() && Slice(it.first).substr(0, prefix.size()) == prefix) {
        keys.push_back(it.first);
      }
    }
    for (const auto &key : keys) {
      map_.erase(key);
    }
  }

  void force_sync(Promise<> &&promise, const char *source) final {
    static_cast<void>(source);
    promise.set_value(Unit());
  }

  void close(Promise<> promise) final {
    promise.set_value(Unit());
  }

  size_t get_calls{0};
  size_t prefix_get_calls{0};
  size_t set_calls{0};
  size_t erase_calls{0};
  size_t erase_batch_calls{0};

  void reset_counters() {
    get_calls = 0;
    prefix_get_calls = 0;
    set_calls = 0;
    erase_calls = 0;
    erase_batch_calls = 0;
  }

 private:
  SeqNo seq_no_{0};
  std::unordered_map<string, string, Hash<string>> map_;
};

NetworkRouteHints non_ru_route() {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;
  return route;
}

TEST(EchRouteFailureLookupBudgetAdversarial, ShortDestinationLookupFanoutStaysBounded) {
  auto store = std::make_shared<CountingEchRouteFailureStore>();
  ScopedRuntimeEchStore scoped_store(store);

  td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests();

  // "a" is intentionally adversarial: without bounded dotted alias lookup,
  // this destination fan-outs into hundreds of direct+legacy key probes.
  const auto decision = td::mtproto::stealth::get_runtime_ech_decision("a", 2 * 86400, non_ru_route());

  ASSERT_TRUE(decision.ech_mode == EchMode::Rfc9180Outer);
  ASSERT_FALSE(decision.disabled_by_route);
  ASSERT_FALSE(decision.disabled_by_circuit_breaker);

  // Security contract: lookups must stay bounded for hostile short destinations.
  ASSERT_TRUE(store->get_calls <= static_cast<size_t>(128));
}

TEST(EchRouteFailureLookupBudgetAdversarial, ThreeDotAliasStillLoadsCircuitBreakerState) {
  auto store = std::make_shared<CountingEchRouteFailureStore>();
  ScopedRuntimeEchStore scoped_store(store);

  td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests();

  const auto persisted_key = td::string("stealth_ech_cb#victim.example...");
  store->set(persisted_key, serialize_store_entry(/*failures=*/5, /*blocked=*/true,
                                                  /*remaining_ms=*/120000, now_system_ms()));

  const RuntimeEchDecision decision =
      td::mtproto::stealth::get_runtime_ech_decision("victim.example", 2 * 86400, non_ru_route());

  ASSERT_TRUE(decision.disabled_by_circuit_breaker);
  ASSERT_TRUE(decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(store->get_calls <= static_cast<size_t>(128));
}

TEST(EchRouteFailureLookupBudgetAdversarial, EmptyDestinationFailsClosedWithoutStoreLookups) {
  auto store = std::make_shared<CountingEchRouteFailureStore>();
  ScopedRuntimeEchStore scoped_store(store);

  td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests();
  store->reset_counters();

  const RuntimeEchDecision decision = td::mtproto::stealth::get_runtime_ech_decision("", 2 * 86400, non_ru_route());

  ASSERT_TRUE(decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(decision.disabled_by_route);
  ASSERT_FALSE(decision.disabled_by_circuit_breaker);
  ASSERT_EQ(static_cast<size_t>(0), store->get_calls);
  ASSERT_EQ(static_cast<size_t>(0), store->prefix_get_calls);
}

TEST(EchRouteFailureLookupBudgetAdversarial, DegenerateDotOnlyDestinationFailsClosedWithoutStoreMutation) {
  auto store = std::make_shared<CountingEchRouteFailureStore>();
  ScopedRuntimeEchStore scoped_store(store);

  td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests();
  store->reset_counters();

  td::mtproto::stealth::note_runtime_ech_failure("....", 2 * 86400);
  td::mtproto::stealth::note_runtime_ech_success("....", 2 * 86400);

  ASSERT_EQ(static_cast<size_t>(0), store->set_calls);
  ASSERT_EQ(static_cast<size_t>(0), store->erase_calls);
  ASSERT_EQ(static_cast<size_t>(0), store->erase_batch_calls);
  ASSERT_EQ(static_cast<size_t>(0), store->get_calls);
  ASSERT_EQ(static_cast<size_t>(0), store->prefix_get_calls);
}

}  // namespace
}  // namespace td::mtproto::test
