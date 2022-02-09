//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/tests.h"

#include <array>
#include "td/utils/Random.h"
#include "td/utils/algorithm.h"

TEST(FlatHashMap, basic) {
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

  td::FlatHashMap<int, std::array<td::unique_ptr<td::string>, 20>> x;
  auto y = std::move(x);
  x[12];
  x.erase(x.find(12));

  {
    td::FlatHashMap<int, std::string> map = {{1, "hello"}, {2, "world"}};
    ASSERT_EQ("hello", map[1]);
    ASSERT_EQ("world", map[2]);
    ASSERT_EQ(2u, map.size());
    ASSERT_EQ("", map[3]);
    ASSERT_EQ(3u, map.size());
  }

  {
    td::FlatHashMap<int, std::string> map = {{1, "hello"}, {1, "world"}};
    ASSERT_EQ("world", map[1]);
    ASSERT_EQ(1u, map.size());
  }
}

template <class T>
auto extract_kv(const T &reference) {
  auto expected = td::transform(reference, [](auto &it) {return std::make_pair(it.first, it.second);});
  std::sort(expected.begin(), expected.end());
  return expected;
}

TEST(FlatHashMap, remove_if_basic) {
  td::Random::Xorshift128plus rnd(123);

  for (int test_i = 0; test_i < 1000000; test_i++) {
    std::unordered_map<td::uint64, td::uint64> reference;
    td::FlatHashMap<td::uint64, td::uint64> table;
    LOG_IF(ERROR, test_i % 1000 == 0) << test_i;
    int N = rnd.fast(1, 1000);
    for (int i = 0; i < N; i++) {
      auto key = rnd();
      auto value = i;
      reference[key] = value;
      table[key] = value;
    }
    ASSERT_EQ(extract_kv(reference), extract_kv(table));

    std::vector<std::pair<td::uint64, td::uint64>> kv;
    td::table_remove_if(table, [&](auto &it) {kv.emplace_back(it.first, it.second); return it.second % 2 == 0; });
    std::sort(kv.begin(), kv.end());
    ASSERT_EQ(extract_kv(reference), kv);

    td::table_remove_if(reference, [](auto &it) {return it.second % 2 == 0;});
    ASSERT_EQ(extract_kv(reference), extract_kv(table));
  }
}
