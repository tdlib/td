//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/emoji.h"
#include "td/utils/tests.h"

TEST(Misc, is_emoji) {
  ASSERT_TRUE(td::is_emoji("👩🏼‍❤‍💋‍👩🏻"));
  ASSERT_TRUE(td::is_emoji("👩🏼‍❤️‍💋‍👩🏻"));
  ASSERT_TRUE(!td::is_emoji("👩🏼‍❤️️‍💋‍👩🏻"));
  ASSERT_TRUE(td::is_emoji("⌚"));
  ASSERT_TRUE(td::is_emoji("↔"));
  ASSERT_TRUE(td::is_emoji("🪗"));
  ASSERT_TRUE(td::is_emoji("2️⃣"));
  ASSERT_TRUE(td::is_emoji("2⃣"));
  ASSERT_TRUE(!td::is_emoji(" 2⃣"));
  ASSERT_TRUE(!td::is_emoji("2⃣ "));
  ASSERT_TRUE(!td::is_emoji(" "));
  ASSERT_TRUE(!td::is_emoji(""));
  ASSERT_TRUE(!td::is_emoji("1234567890123456789012345678901234567890123456789012345678901234567890"));
  ASSERT_TRUE(td::is_emoji("❤️"));
  ASSERT_TRUE(td::is_emoji("❤"));
  ASSERT_TRUE(td::is_emoji("⌚"));
  ASSERT_TRUE(td::is_emoji("🎄"));
  ASSERT_TRUE(td::is_emoji("🧑‍🎄"));
}
