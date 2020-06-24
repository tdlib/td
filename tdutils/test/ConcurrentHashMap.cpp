//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/benchmark.h"
#include "td/utils/ConcurrentHashTable.h"
#include "td/utils/misc.h"
#include "td/utils/port/thread.h"
#include "td/utils/SpinLock.h"
#include "td/utils/tests.h"

#include <atomic>
#include <mutex>

#if !TD_THREAD_UNSUPPORTED

#if TD_HAVE_ABSL
#include <absl/container/flat_hash_map.h>
#else
#include <unordered_map>
#endif

#if TD_WITH_LIBCUCKOO
#include <third-party/libcuckoo/libcuckoo/cuckoohash_map.hh>
#endif

#if TD_WITH_JUNCTION
#include <junction/ConcurrentMap_Grampa.h>
#include <junction/ConcurrentMap_Leapfrog.h>
#include <junction/ConcurrentMap_Linear.h>
#endif

namespace td {

// Non resizable HashMap. Just an example
template <class KeyT, class ValueT>
class ArrayHashMap {
 public:
  explicit ArrayHashMap(size_t n) : array_(n) {
  }
  struct Node {
    std::atomic<KeyT> key{KeyT{}};
    std::atomic<ValueT> value{ValueT{}};
  };
  static std::string get_name() {
    return "ArrayHashMap";
  }
  KeyT empty_key() const {
    return KeyT{};
  }

  void insert(KeyT key, ValueT value) {
    array_.with_value(key, true, [&](auto &node_value) { node_value.store(value, std::memory_order_release); });
  }
  ValueT find(KeyT key, ValueT value) {
    array_.with_value(key, false, [&](auto &node_value) { value = node_value.load(std::memory_order_acquire); });
    return value;
  }

 private:
  AtomicHashArray<KeyT, std::atomic<ValueT>> array_;
};

template <class KeyT, class ValueT>
class ConcurrentHashMapMutex {
 public:
  explicit ConcurrentHashMapMutex(size_t) {
  }
  static std::string get_name() {
    return "ConcurrentHashMapMutex";
  }
  void insert(KeyT key, ValueT value) {
    std::unique_lock<std::mutex> lock(mutex_);
    hash_map_.emplace(key, value);
  }
  ValueT find(KeyT key, ValueT default_value) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = hash_map_.find(key);
    if (it == hash_map_.end()) {
      return default_value;
    }
    return it->second;
  }

 private:
  std::mutex mutex_;
#if TD_HAVE_ABSL
  absl::flat_hash_map<KeyT, ValueT> hash_map_;
#else
  std::unordered_map<KeyT, ValueT> hash_map_;
#endif
};

template <class KeyT, class ValueT>
class ConcurrentHashMapSpinlock {
 public:
  explicit ConcurrentHashMapSpinlock(size_t) {
  }
  static std::string get_name() {
    return "ConcurrentHashMapSpinlock";
  }
  void insert(KeyT key, ValueT value) {
    auto guard = spinlock_.lock();
    hash_map_.emplace(key, value);
  }
  ValueT find(KeyT key, ValueT default_value) {
    auto guard = spinlock_.lock();
    auto it = hash_map_.find(key);
    if (it == hash_map_.end()) {
      return default_value;
    }
    return it->second;
  }

 private:
  SpinLock spinlock_;
#if TD_HAVE_ABSL
  absl::flat_hash_map<KeyT, ValueT> hash_map_;
#else
  std::unordered_map<KeyT, ValueT> hash_map_;
#endif
};

#if TD_WITH_LIBCUCKOO
template <class KeyT, class ValueT>
class ConcurrentHashMapLibcuckoo {
 public:
  explicit ConcurrentHashMapLibcuckoo(size_t) {
  }
  static std::string get_name() {
    return "ConcurrentHashMapLibcuckoo";
  }
  void insert(KeyT key, ValueT value) {
    hash_map_.insert(key, value);
  }
  ValueT find(KeyT key, ValueT default_value) {
    hash_map_.find(key, default_value);
    return default_value;
  }

 private:
  cuckoohash_map<KeyT, ValueT> hash_map_;
};
#endif

#if TD_WITH_JUNCTION
template <class KeyT, class ValueT>
class ConcurrentHashMapJunction {
 public:
  explicit ConcurrentHashMapJunction(size_t size) : hash_map_() {
  }
  static std::string get_name() {
    return "ConcurrentHashMapJunction";
  }
  void insert(KeyT key, ValueT value) {
    hash_map_.assign(key, value);
  }
  ValueT find(KeyT key, ValueT default_value) {
    return hash_map_.get(key);
  }

  ConcurrentHashMapJunction(const ConcurrentHashMapJunction &) = delete;
  ConcurrentHashMapJunction &operator=(const ConcurrentHashMapJunction &) = delete;
  ConcurrentHashMapJunction(ConcurrentHashMapJunction &&other) = delete;
  ConcurrentHashMapJunction &operator=(ConcurrentHashMapJunction &&) = delete;
  ~ConcurrentHashMapJunction() {
    junction::DefaultQSBR.flush();
  }

 private:
  junction::ConcurrentMap_Leapfrog<KeyT, ValueT> hash_map_;
};
#endif

}  // namespace td

template <class HashMap>
class HashMapBenchmark : public td::Benchmark {
  struct Query {
    int key;
    int value;
  };
  std::vector<Query> queries;
  td::unique_ptr<HashMap> hash_map;

  size_t threads_n = 16;
  int mod_;
  static constexpr size_t MUL = 7273;  //1000000000 + 7;
  int n_;

 public:
  explicit HashMapBenchmark(size_t threads_n) : threads_n(threads_n) {
  }
  std::string get_description() const override {
    return HashMap::get_name();
  }
  void start_up_n(int n) override {
    n *= static_cast<int>(threads_n);
    n_ = n;
    hash_map = td::make_unique<HashMap>(n * 2);
  }

  void run(int n) override {
    n = n_;
    std::vector<td::thread> threads;

    for (size_t i = 0; i < threads_n; i++) {
      size_t l = n * i / threads_n;
      size_t r = n * (i + 1) / threads_n;
      threads.emplace_back([l, r, this] {
        for (size_t i = l; i < r; i++) {
          auto x = td::narrow_cast<int>((i + 1) * MUL % n_) + 3;
          auto y = td::narrow_cast<int>(i + 2);
          hash_map->insert(x, y);
        }
      });
    }
    for (auto &thread : threads) {
      thread.join();
    }
  }

  void tear_down() override {
    for (int i = 0; i < n_; i++) {
      auto x = td::narrow_cast<int>((i + 1) * MUL % n_) + 3;
      auto y = td::narrow_cast<int>(i + 2);
      ASSERT_EQ(y, hash_map->find(x, -1));
    }
    queries.clear();
    hash_map.reset();
  }
};

template <class HashMap>
static void bench_hash_map() {
  td::bench(HashMapBenchmark<HashMap>(16));
  td::bench(HashMapBenchmark<HashMap>(1));
}

TEST(ConcurrentHashMap, Benchmark) {
  bench_hash_map<td::ConcurrentHashMap<int, int>>();
  bench_hash_map<td::ArrayHashMap<int, int>>();
  bench_hash_map<td::ConcurrentHashMapSpinlock<int, int>>();
  bench_hash_map<td::ConcurrentHashMapMutex<int, int>>();
#if TD_WITH_LIBCUCKOO
  bench_hash_map<td::ConcurrentHashMapLibcuckoo<int, int>>();
#endif
#if TD_WITH_JUNCTION
  bench_hash_map<td::ConcurrentHashMapJunction<int, int>>();
#endif
}

#endif
