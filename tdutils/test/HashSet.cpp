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
