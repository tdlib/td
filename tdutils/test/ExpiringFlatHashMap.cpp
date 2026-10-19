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
