//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/common.h"
#include "td/utils/ExpiringFlatHashMap.h"
#include "td/utils/tests.h"
#include "td/utils/Time.h"

TEST(ExpiringFlatHashMap, basic) {
  td::ExpiringFlatHashMap<int, td::string> map(1e9);
  ASSERT_TRUE(map.empty());
  ASSERT_TRUE(map.get(1) == nullptr);
  map.set(1, "a");
  map.set(2, "b");
  ASSERT_EQ(2u, map.size());
  ASSERT_EQ("a", *map.get(1));
  map.set(1, "c");
  ASSERT_EQ("c", *map.get(1));
  ASSERT_EQ(1u, map.erase(2));
  ASSERT_EQ(0u, map.erase(2));
  ASSERT_TRUE(map.get(2) == nullptr);
  ASSERT_EQ(1u, map.size());
  map.clear();
  ASSERT_TRUE(map.empty());
}

TEST(ExpiringFlatHashMap, expiration) {
  td::ExpiringFlatHashMap<int, int> map(10.0);
  for (int i = 1; i <= 25; i++) {
    map.set(i, i);
  }
  ASSERT_EQ(25u, map.size());
  td::Time::jump_in_future(td::Time::now() + 6.0);
  ASSERT_EQ(1, *map.get(1));
  td::Time::jump_in_future(td::Time::now() + 6.0);
  // items 2-25 are expired, but only 10 of them can be dropped during a call
  ASSERT_TRUE(map.get(26) == nullptr);
  ASSERT_EQ(15u, map.size());
  ASSERT_TRUE(map.get(26) == nullptr);
  ASSERT_EQ(5u, map.size());
  ASSERT_TRUE(map.get(26) == nullptr);
  ASSERT_EQ(1u, map.size());
  ASSERT_EQ(1, *map.get(1));
}

TEST(ExpiringFlatHashMap, operator_brackets) {
  td::ExpiringFlatHashMap<int, int> map(10.0);
  ASSERT_EQ(0, map[1]);
  ASSERT_EQ(1u, map.size());
  map[1] = 5;
  map[2]++;
  ASSERT_EQ(5, *map.get(1));
  ASSERT_EQ(1, map[2]);
  ASSERT_EQ(2u, map.size());
  td::Time::jump_in_future(td::Time::now() + 6.0);
  map[1] += 1;
  td::Time::jump_in_future(td::Time::now() + 6.0);
  // item 2 is expired, but item 1 was accessed through operator[] recently
  ASSERT_TRUE(map.get(3) == nullptr);
  ASSERT_EQ(1u, map.size());
  ASSERT_EQ(6, map[1]);
  ASSERT_EQ(0, map[2]);
  ASSERT_EQ(2u, map.size());

  td::ExpiringFlatHashMap<td::string, td::string> string_map(10.0);
  string_map["a"] += "b";
  string_map["a"] += "c";
  ASSERT_EQ("bc", *string_map.get("a"));
  ASSERT_TRUE(string_map["b"].empty());
  ASSERT_EQ(2u, string_map.size());
}

TEST(ExpiringFlatHashMap, clear) {
  td::ExpiringFlatHashMap<int, int> map(10.0);
  map.clear();
  ASSERT_TRUE(map.empty());
  for (int i = 1; i <= 25; i++) {
    map.set(i, i);
  }
  map.clear();
  ASSERT_TRUE(map.empty());
  ASSERT_EQ(0u, map.size());
  ASSERT_TRUE(map.get(1) == nullptr);
  ASSERT_EQ(0u, map.erase(2));

  map.set(1, 10);
  map[2] = 20;
  ASSERT_EQ(10, *map.get(1));
  ASSERT_EQ(20, map[2]);
  ASSERT_EQ(0, map[3]);
  ASSERT_EQ(3u, map.size());
  td::Time::jump_in_future(td::Time::now() + 6.0);
  ASSERT_EQ(10, *map.get(1));
  td::Time::jump_in_future(td::Time::now() + 6.0);
  // only items added after clear are dropped
  ASSERT_TRUE(map.get(4) == nullptr);
  ASSERT_EQ(1u, map.size());
  ASSERT_EQ(10, *map.get(1));

  map.clear();
  map.clear();
  ASSERT_TRUE(map.empty());
  map.set(5, 50);
  ASSERT_EQ(1u, map.size());
  ASSERT_EQ(50, *map.get(5));
  ASSERT_EQ(1u, map.erase(5));
  ASSERT_TRUE(map.empty());
}
