//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/Enumerator.h"
#include "td/utils/tests.h"

TEST(Enumerator, simple) {
  td::Enumerator<std::string> e;
  auto b = e.add("b");
  auto a = e.add("a");
  auto d = e.add("d");
  auto c = e.add("c");
  ASSERT_STREQ(e.get(a), "a");
  ASSERT_STREQ(e.get(b), "b");
  ASSERT_STREQ(e.get(c), "c");
  ASSERT_STREQ(e.get(d), "d");
  ASSERT_EQ(a, e.add("a"));
  ASSERT_EQ(b, e.add("b"));
  ASSERT_EQ(c, e.add("c"));
  ASSERT_EQ(d, e.add("d"));
}
