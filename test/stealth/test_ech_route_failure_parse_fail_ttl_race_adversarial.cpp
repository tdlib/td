// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

// Adversarial integration test:
// A malformed persisted ECH route-failure entry must be fail-closed, but the
// resulting disabled_until must still be clamped to the TTL snapshot used by
// the in-flight decision call.

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "test/stealth/ech_route_failure_store_test_utils.h"

#include "td/utils/tests.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace ech_route_failure_parse_fail_ttl_race_adversarial {

using td::int32;
using td::int64;
using td::KeyValueSyncInterface;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::get_runtime_ech_decision;
using td::mtproto::stealth::reset_runtime_ech_counters_for_tests;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthRuntimeParams;

class RuntimeGuard final {
 public:
  RuntimeGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }
};

StealthRuntimeParams make_runtime_params(double ttl_seconds, td::uint32 threshold = 1) {
  auto params = td::mtproto::stealth::default_runtime_stealth_params();
  params.route_failure.ech_failure_threshold = threshold;
  params.route_failure.ech_disable_ttl_seconds = ttl_seconds;
  return params;
}

int64 parse_remaining_ms(td::Slice serialized) {
  auto first = serialized.find('|');
  CHECK(first != td::Slice::npos);
  auto second = serialized.substr(first + 1).find('|');
  CHECK(second != td::Slice::npos);
  second += first + 1;
  auto third = serialized.substr(second + 1).find('|');
  CHECK(third != td::Slice::npos);
  third += second + 1;

  auto remaining = serialized.substr(second + 1, third - second - 1).str();
  CHECK(!remaining.empty());

  char *end = nullptr;
  auto parsed = std::strtoll(remaining.c_str(), &end, 10);
  CHECK(end != nullptr && *end == '\0');
  return static_cast<int64>(parsed);
}

class BlockingReadStore final : public KeyValueSyncInterface {
 public:
  void set_block_key(td::string key) {
    auto lock = std::scoped_lock(mutex_);
    blocked_key_ = std::move(key);
  }

  bool wait_until_blocked() {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, std::chrono::seconds(5), [&] { return blocked_get_entered_; });
  }

  void release_blocked_get() {
    auto lock = std::scoped_lock(mutex_);
    blocked_get_released_ = true;
    cv_.notify_all();
  }

  SeqNo set(td::string key, td::string value) final {
    auto lock = std::scoped_lock(mutex_);
    map_[std::move(key)] = std::move(value);
    return ++seq_no_;
  }

  bool isset(const td::string &key) final {
    auto lock = std::scoped_lock(mutex_);
    return map_.count(key) != 0;
  }

  td::string get(const td::string &key) final {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!blocked_key_.empty() && key == blocked_key_ && !blocked_get_released_) {
      blocked_get_entered_ = true;
      cv_.notify_all();
      cv_.wait(lock, [&] { return blocked_get_released_; });
    }

    auto it = map_.find(key);
    return it == map_.end() ? td::string() : it->second;
  }

  void for_each(std::function<void(td::Slice, td::Slice)> func) final {
    auto lock = std::scoped_lock(mutex_);
    for (const auto &it : map_) {
      func(it.first, it.second);
    }
  }

  std::unordered_map<td::string, td::string, td::Hash<td::string>> prefix_get(td::Slice prefix) final {
    auto lock = std::scoped_lock(mutex_);
    std::unordered_map<td::string, td::string, td::Hash<td::string>> result;
    for (const auto &it : map_) {
      if (prefix.size() <= it.first.size() && td::Slice(it.first).substr(0, prefix.size()) == prefix) {
        result.emplace(it.first, it.second);
      }
    }
    return result;
  }

  td::FlatHashMap<td::string, td::string> get_all() final {
    auto lock = std::scoped_lock(mutex_);
    td::FlatHashMap<td::string, td::string> result;
    for (const auto &it : map_) {
      result.emplace(it.first, it.second);
    }
    return result;
  }

  SeqNo erase(const td::string &key) final {
    auto lock = std::scoped_lock(mutex_);
    map_.erase(key);
    return ++seq_no_;
  }

  SeqNo erase_batch(td::vector<td::string> keys) final {
    auto lock = std::scoped_lock(mutex_);
    for (const auto &key : keys) {
      map_.erase(key);
    }
    return ++seq_no_;
  }

  void erase_by_prefix(td::Slice prefix) final {
    auto lock = std::scoped_lock(mutex_);
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
    static_cast<void>(source);
    promise.set_value(td::Unit());
  }

  void close(td::Promise<> promise) final {
    promise.set_value(td::Unit());
  }

 private:
  SeqNo seq_no_{0};
  std::unordered_map<td::string, td::string, td::Hash<td::string>> map_;
  std::mutex mutex_;
  std::condition_variable cv_;
  td::string blocked_key_;
  bool blocked_get_entered_{false};
  bool blocked_get_released_{false};
};

TEST(EchRouteFailureParseFailTtlRaceAdversarial,
     MalformedStoreEntryIsClampedToDecisionSnapshotTtlUnderConcurrentRuntimeReload) {
  RuntimeGuard guard;

  auto store = std::make_shared<BlockingReadStore>();
  td::mtproto::test::ScopedRuntimeEchStore scoped_store(store);

  const td::Slice destination("parse-fail-race.example.com");
  constexpr int32 kUnixTime = 1712345678;
  const auto key = td::mtproto::test::canonical_store_key(destination);

  // Malformed payload to force parse failure and fail-closed fallback branch.
  store->set(key, "not-a-valid-route-failure-entry");
  store->set_block_key(key);

  ASSERT_TRUE(set_runtime_stealth_params_for_tests(make_runtime_params(60.0, 1)).is_ok());

  td::mtproto::stealth::RuntimeEchDecision decision;
  std::thread reader(
      [&] { decision = get_runtime_ech_decision(destination, kUnixTime, td::mtproto::test::non_ru_route_hints()); });

  ASSERT_TRUE(store->wait_until_blocked());

  // Change runtime TTL while decision call is in flight. The decision's
  // max-disabled-until snapshot remains at 60s and must clamp fail-closed
  // insertion even if current runtime params become 86400s.
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(make_runtime_params(86400.0, 1)).is_ok());

  store->release_blocked_get();
  reader.join();

  ASSERT_TRUE(decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(decision.disabled_by_circuit_breaker);

  auto persisted = store->get(key);
  ASSERT_TRUE(!persisted.empty());

  auto remaining_ms = parse_remaining_ms(persisted);
  ASSERT_TRUE(remaining_ms > 0);
  ASSERT_TRUE(remaining_ms <= 65000);
}

TEST(EchRouteFailureParseFailTtlRaceAdversarial,
     BlockedStoreReadDoesNotSerializeIndependentRuntimeDecisionLookups) {
  RuntimeGuard guard;

  auto store = std::make_shared<BlockingReadStore>();
  td::mtproto::test::ScopedRuntimeEchStore scoped_store(store);

  const td::Slice blocked_destination("blocked-read.example.com");
  const td::Slice independent_destination("independent-read.example.com");
  constexpr int32 kUnixTime = 1712345678;
  const auto blocked_key = td::mtproto::test::canonical_store_key(blocked_destination);

  store->set(blocked_key, td::mtproto::test::serialize_store_entry(/*failures=*/5, /*blocked=*/true,
                                                                   /*remaining_ms=*/60000,
                                                                   td::mtproto::test::now_system_ms()));
  store->set_block_key(blocked_key);

  ASSERT_TRUE(set_runtime_stealth_params_for_tests(make_runtime_params(60.0, 1)).is_ok());

  td::mtproto::stealth::RuntimeEchDecision blocked_decision;
  std::thread blocked_reader([&] {
    blocked_decision = get_runtime_ech_decision(blocked_destination, kUnixTime, td::mtproto::test::non_ru_route_hints());
  });

  ASSERT_TRUE(store->wait_until_blocked());

  td::mtproto::stealth::RuntimeEchDecision independent_decision;
  std::mutex independent_mutex;
  std::condition_variable independent_cv;
  bool independent_ready = false;
  std::thread independent_reader([&] {
    auto decision = get_runtime_ech_decision(independent_destination, kUnixTime, td::mtproto::test::non_ru_route_hints());
    {
      auto lock = std::lock_guard(independent_mutex);
      independent_decision = decision;
      independent_ready = true;
    }
    independent_cv.notify_one();
  });

  {
    auto lock = std::unique_lock(independent_mutex);
    ASSERT_TRUE(independent_cv.wait_for(lock, std::chrono::milliseconds(250), [&] { return independent_ready; }));
  }
  independent_reader.join();

  ASSERT_TRUE(independent_decision.ech_mode == EchMode::Rfc9180Outer);
  ASSERT_FALSE(independent_decision.disabled_by_route);
  ASSERT_FALSE(independent_decision.disabled_by_circuit_breaker);

  store->release_blocked_get();
  blocked_reader.join();

  ASSERT_TRUE(blocked_decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(blocked_decision.disabled_by_circuit_breaker);
}

}  // namespace ech_route_failure_parse_fail_ttl_race_adversarial
