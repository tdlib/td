//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "memprof/memprof.h"

#include "td/utils/check.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/format.h"
#include "td/utils/Slice.h"
#include "td/utils/UInt.h"

#include <absl/container/flat_hash_map.h>
#include <folly/container/F14Map.h>
#include <map>
#include <unordered_map>

template <class T>
class Generator {
 public:
  T next() {
    UNREACHABLE();
  }
};

template <class T>
class IntGenerator {
 public:
  T next() {
    return ++value;
  }

 private:
  T value{};
};

template <>
class Generator<uint32_t> : public IntGenerator<uint32_t> {
 public:
};
template <>
class Generator<uint64_t> : public IntGenerator<uint64_t> {
 public:
};

template <class T, class KeyT, class ValueT>
void measure(td::StringBuilder &sb, td::Slice name, td::Slice key_name, td::Slice value_name) {
  sb << name << "<" << key_name << "," << value_name << ">:\n";
  size_t ideal_size = sizeof(KeyT) + sizeof(ValueT);

  sb << "\tempty:" << sizeof(T);
  struct Stat {
    int pi;
    double min_ratio;
    double max_ratio;
  };
  std::vector<Stat> stat;
  stat.reserve(1024);
  for (size_t size : {10000000u}) {
    Generator<KeyT> key_generator;
    auto start_mem = get_used_memory_size();
    T ht;
    auto ratio = [&]() {
      auto end_mem = get_used_memory_size();
      auto used_mem = end_mem - start_mem;
      return double(used_mem) / double(ideal_size * ht.size());
    };
    double min_ratio;
    double max_ratio;
    auto reset = [&]() {
      min_ratio = 1e100;
      max_ratio = 0;
    };
    auto update = [&]() {
      auto x = ratio();
      min_ratio = std::min(min_ratio, x);
      max_ratio = std::max(max_ratio, x);
      if (x > 14) {
        LOG(ERROR) << "WTF";
      }
    };
    reset();

    int p = 10;
    int pi = 1;
    for (size_t i = 0; i < size; i++) {
      ht.emplace(key_generator.next(), ValueT{});
      update();
      if ((i + 1) % p == 0) {
        stat.emplace_back(Stat{pi, min_ratio, max_ratio});
        reset();
        pi++;
        p *= 10;
      }
    }
  }
  for (auto &s : stat) {
    sb << " " << 10 << "^" << s.pi << ":" << s.min_ratio << "->" << s.max_ratio;
  }
  sb << "\n";
}

template <template <typename... Args> class T>
void print_memory_stats(td::Slice name) {
  std::string big_buff(1 << 16, '\0');
  td::StringBuilder sb(big_buff, false);
#define MEASURE(KeyT, ValueT) measure<T<KeyT, ValueT>, KeyT, ValueT>(sb, name, #KeyT, #ValueT);
  MEASURE(uint32_t, uint32_t)
  // MEASURE(uint64_t, td::UInt256)
  LOG(ERROR) << "\n" << sb.as_cslice();
}

#define FOR_EACH_TABLE(F) \
  F(td::FlatHashMapImpl)  \
  F(folly::F14FastMap)    \
  F(absl::flat_hash_map)  \
  F(std::unordered_map)   \
  F(std::map)
#define BENCH_MEMORY(T) print_memory_stats<T>(#T);

int main() {
  CHECK(get_used_memory_size());
  FOR_EACH_TABLE(BENCH_MEMORY);
  return 0;
}