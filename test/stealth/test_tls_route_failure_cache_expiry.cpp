//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "tddb/td/db/KeyValueSyncInterface.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::note_runtime_ech_failure;
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

NetworkRouteHints known_non_ru_route() {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;
  return hints;
}

TEST(TlsRouteFailureCacheExpiry, ExpiredPersistedCircuitBreakerStateMustNotDisableEch) {
  auto store = std::make_shared<MemoryKeyValue>();
  ScopedRuntimeEchStore scoped_store(store);

  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();

  const td::string destination = "persist.example.com";
  const td::int32 unix_time = 1712345678;
  store->set("stealth_ech_cb#persist.example.com|19818", "3|1|0|0");

  ASSERT_TRUE(EchMode::Rfc9180Outer == runtime_ech_mode_for_route(destination, unix_time, known_non_ru_route()));
}

TEST(TlsRouteFailureCacheExpiry, ExpiredPersistedPartialFailuresMustNotAccumulateAcrossRestart) {
  auto store = std::make_shared<MemoryKeyValue>();
  ScopedRuntimeEchStore scoped_store(store);

  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();

  const td::string destination = "persist.example.com";
  const td::int32 unix_time = 1712345678;
  store->set("stealth_ech_cb#persist.example.com|19818", "2|0|0|0");

  ASSERT_TRUE(EchMode::Rfc9180Outer == runtime_ech_mode_for_route(destination, unix_time, known_non_ru_route()));

  note_runtime_ech_failure(destination, unix_time);
  ASSERT_TRUE(EchMode::Rfc9180Outer == runtime_ech_mode_for_route(destination, unix_time, known_non_ru_route()));
}

}  // namespace