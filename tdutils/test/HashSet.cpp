//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/algorithm.h"
#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>

template <class T>
static auto extract_kv(const T &reference) {
  auto expected = td::transform(reference, [](auto &it) { return std::make_pair(it.first, it.second); });
  std::sort(expected.begin(), expected.end());
  return expected;
}

TEST(FlatHashMap, basic) {
  {
      td::FlatHashSet<int> s;
      s.insert(5);
      for (auto x : s) {
      }
      int N = 100000;
      for (int i = 0; i < 10000000; i++) {
        s.insert((i + N/2)%N);
        s.erase(i%N);
      }
  }
  {
    td::FlatHashMap<int, int> map;
    map[1] = 2;
    ASSERT_EQ(2, map[1]);
    ASSERT_EQ(1, map.find(1)->first);
    ASSERT_EQ(2, map.find(1)->second);
    // ASSERT_EQ(1, map.find(1)->key());
    // ASSERT_EQ(2, map.find(1)->value());
    for (auto &kv : map) {
      ASSERT_EQ(1, kv.first);
      ASSERT_EQ(2, kv.second);
    }
    map.erase(map.find(1));
    auto map_copy = map;
  }

  td::FlatHashMap<int, std::array<td::unique_ptr<td::string>, 10>> x;
  auto y = std::move(x);
  x[12];
  x.erase(x.find(12));

  {
    td::FlatHashMap<int, td::string> map = {{1, "hello"}, {2, "world"}};
    ASSERT_EQ("hello", map[1]);
    ASSERT_EQ("world", map[2]);
    ASSERT_EQ(2u, map.size());
    ASSERT_EQ("", map[3]);
    ASSERT_EQ(3u, map.size());
  }

  {
    td::FlatHashMap<int, td::string> map = {{1, "hello"}, {1, "world"}};
    ASSERT_EQ("world", map[1]);
    ASSERT_EQ(1u, map.size());
  }

  using KV = td::FlatHashMapImpl<td::string, td::string>;
  using Data = td::vector<std::pair<td::string, td::string>>;
  auto data = Data{{"a", "b"}, {"c", "d"}};
  { ASSERT_EQ(Data{}, extract_kv(KV())); }

  {
    KV kv(data.begin(), data.end());
    ASSERT_EQ(data, extract_kv(kv));

    KV copied_kv(kv);
    ASSERT_EQ(data, extract_kv(copied_kv));

    KV moved_kv(std::move(kv));
    ASSERT_EQ(data, extract_kv(moved_kv));
    ASSERT_EQ(Data{}, extract_kv(kv));
    ASSERT_TRUE(kv.empty());
    kv = std::move(moved_kv);
    ASSERT_EQ(data, extract_kv(kv));

    KV assign_copied_kv;
    assign_copied_kv = kv;
    ASSERT_EQ(data, extract_kv(assign_copied_kv));

    KV assign_moved_kv;
    assign_moved_kv = std::move(kv);
    ASSERT_EQ(data, extract_kv(assign_moved_kv));
    ASSERT_EQ(Data{}, extract_kv(kv));
    ASSERT_TRUE(kv.empty());
    kv = std::move(assign_moved_kv);

    KV it_copy_kv(kv.begin(), kv.end());
    ASSERT_EQ(data, extract_kv(it_copy_kv));
  }

  {
    KV kv;
    ASSERT_TRUE(kv.empty());
    ASSERT_EQ(0u, kv.size());
    kv = KV(data.begin(), data.end());
    ASSERT_TRUE(!kv.empty());
    ASSERT_EQ(2u, kv.size());

    ASSERT_EQ("a", kv.find("a")->first);
    ASSERT_EQ("b", kv.find("a")->second);
    ASSERT_EQ("a", kv.find("a")->key());
    ASSERT_EQ("b", kv.find("a")->value());
    kv.find("a")->second = "c";
    ASSERT_EQ("c", kv.find("a")->second);
    ASSERT_EQ("c", kv["a"]);

    ASSERT_EQ(0u, kv.count("x"));
    ASSERT_EQ(1u, kv.count("a"));
  }
  {
    KV kv;
    kv["d"];
    ASSERT_EQ((Data{{"d", ""}}), extract_kv(kv));
    kv.erase(kv.find("d"));
    ASSERT_EQ(Data{}, extract_kv(kv));
  }
}

TEST(FlatHashMap, remove_if_basic) {
  td::Random::Xorshift128plus rnd(123);

  constexpr int TESTS_N = 10000;
  constexpr int MAX_TABLE_SIZE = 1000;
  for (int test_i = 0; test_i < TESTS_N; test_i++) {
    std::unordered_map<td::uint64, td::uint64> reference;
    td::FlatHashMap<td::uint64, td::uint64> table;
    int N = rnd.fast(1, MAX_TABLE_SIZE);
    for (int i = 0; i < N; i++) {
      auto key = rnd();
      auto value = i;
      reference[key] = value;
      table[key] = value;
    }
    ASSERT_EQ(extract_kv(reference), extract_kv(table));

    td::vector<std::pair<td::uint64, td::uint64>> kv;
    td::table_remove_if(table, [&](auto &it) {
      kv.emplace_back(it.first, it.second);
      return it.second % 2 == 0;
    });
    std::sort(kv.begin(), kv.end());
    ASSERT_EQ(extract_kv(reference), kv);

    td::table_remove_if(reference, [](auto &it) { return it.second % 2 == 0; });
    ASSERT_EQ(extract_kv(reference), extract_kv(table));
  }
}

TEST(FlatHashMap, stress_test) {
  td::vector<td::RandomSteps::Step> steps;
  auto add_step = [&steps](td::Slice, td::uint32 weight, auto f) {
    steps.emplace_back(td::RandomSteps::Step{std::move(f), weight});
  };

  td::Random::Xorshift128plus rnd(123);
  size_t max_table_size = 1000;  // dynamic value
  std::unordered_map<td::uint64, td::uint64> ref;
  td::FlatHashMapImpl<td::uint64, td::uint64> tbl;

  auto validate = [&] {
    ASSERT_EQ(ref.empty(), tbl.empty());
    ASSERT_EQ(ref.size(), tbl.size());
    ASSERT_EQ(extract_kv(ref), extract_kv(tbl));
    for (auto &kv : ref) {
      ASSERT_EQ(ref[kv.first], tbl[kv.first]);
    }
  };
  auto gen_key = [&] {
    auto key = rnd() % 4000 + 1;
    return key;
  };

  add_step("Reset hash table", 1, [&] {
    validate();
    td::reset_to_empty(ref);
    td::reset_to_empty(tbl);
    max_table_size = rnd.fast(1, 1000);
  });
  add_step("Clear hash table", 1, [&] {
    validate();
    ref.clear();
    tbl.clear();
    max_table_size = rnd.fast(1, 1000);
  });

  add_step("Insert random value", 1000, [&] {
    if (tbl.size() > max_table_size) {
      return;
    }
    auto key = gen_key();
    auto value = rnd();
    ref[key] = value;
    tbl[key] = value;
    ASSERT_EQ(ref[key], tbl[key]);
  });

  add_step("Emplace random value", 1000, [&] {
    if (tbl.size() > max_table_size) {
      return;
    }
    auto key = gen_key();
    auto value = rnd();
    auto ref_it = ref.emplace(key, value);
    auto tbl_it = tbl.emplace(key, value);
    ASSERT_EQ(ref_it.second, tbl_it.second);
    ASSERT_EQ(key, tbl_it.first->first);
  });

  add_step("empty operator[]", 1000, [&] {
    if (tbl.size() > max_table_size) {
      return;
    }
    auto key = gen_key();
    ASSERT_EQ(ref[key], tbl[key]);
  });

  add_step("reserve", 10, [&] { tbl.reserve(rnd() % max_table_size); });

  add_step("find", 1000, [&] {
    auto key = gen_key();
    auto ref_it = ref.find(key);
    auto tbl_it = tbl.find(key);
    ASSERT_EQ(ref_it == ref.end(), tbl_it == tbl.end());
    if (ref_it != ref.end()) {
      ASSERT_EQ(ref_it->first, tbl_it->first);
      ASSERT_EQ(ref_it->second, tbl_it->second);
    }
  });

  add_step("find_and_erase", 100, [&] {
    auto key = gen_key();
    auto ref_it = ref.find(key);
    auto tbl_it = tbl.find(key);
    ASSERT_EQ(ref_it == ref.end(), tbl_it == tbl.end());
    if (ref_it != ref.end()) {
      ref.erase(ref_it);
      tbl.erase(tbl_it);
    }
  });

  add_step("remove_if", 5, [&] {
    auto mul = rnd();
    auto bit = rnd() % 64;
    auto condition = [&](auto &it) {
      return (((it.second * mul) >> bit) & 1) == 0;
    };
    td::table_remove_if(tbl, condition);
    td::table_remove_if(ref, condition);
  });

  td::RandomSteps runner(std::move(steps));
  for (size_t i = 0; i < 10000000; i++) {
    runner.step(rnd);
  }
}
