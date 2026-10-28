//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/common.h"
#include "td/utils/emoji.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"

TEST(Emoji, is_emoji) {
  ASSERT_TRUE(!td::is_emoji(""));
  ASSERT_TRUE(td::is_emoji("👩🏼‍❤‍💋‍👩🏻"));
  ASSERT_TRUE(td::is_emoji("👩🏼‍❤‍💋‍👩🏻️"));  // not in RGI emoji ZWJ sequence set
  ASSERT_TRUE(td::is_emoji("👩🏼‍❤️‍💋‍👩🏻"));
  ASSERT_TRUE(td::is_emoji("👩🏼‍❤️‍💋‍👩🏻️"));
  ASSERT_TRUE(!td::is_emoji("👩🏼‍❤️️‍💋‍👩🏻"));
  ASSERT_TRUE(td::is_emoji("⌚"));
  ASSERT_TRUE(td::is_emoji("⌚️"));
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
  ASSERT_TRUE(td::is_emoji("©️"));
  ASSERT_TRUE(td::is_emoji("©"));
  ASSERT_TRUE(!td::is_emoji("©️️"));
  ASSERT_TRUE(td::is_emoji("🕵️‍♂️"));
  ASSERT_TRUE(td::is_emoji("🕵‍♂️"));  // not in RGI emoji ZWJ sequence set
  ASSERT_TRUE(td::is_emoji("🕵️‍♂"));  // not in RGI emoji ZWJ sequence set
  ASSERT_TRUE(td::is_emoji("🕵‍♂"));
  ASSERT_TRUE(td::is_emoji("🏌️‍♂️"));
  ASSERT_TRUE(td::is_emoji("🏋️‍♂️"));
  ASSERT_TRUE(td::is_emoji("🏌‍♂️"));  // not in RGI emoji ZWJ sequence set
  ASSERT_TRUE(td::is_emoji("🏋‍♂️"));  // not in RGI emoji ZWJ sequence set
  ASSERT_TRUE(!td::is_emoji("a‍🤝‍👨"));
  ASSERT_TRUE(!td::is_emoji("👩‍a‍👨"));
  ASSERT_TRUE(!td::is_emoji("👩‍🤝‍a"));
  ASSERT_TRUE(td::is_emoji("👩‍🤝‍👨"));  // not in RGI emoji ZWJ sequence set
}

static void test_get_fitzpatrick_modifier(td::string emoji, int result) {
  ASSERT_EQ(result, td::get_fitzpatrick_modifier(emoji));
}

TEST(Emoji, get_fitzpatrick_modifier) {
  test_get_fitzpatrick_modifier("", 0);
  test_get_fitzpatrick_modifier("👩🏼‍❤‍💋‍👩🏻", 2);
  test_get_fitzpatrick_modifier("👩🏼‍❤️‍💋‍👩🏻", 2);
  test_get_fitzpatrick_modifier("👋", 0);
  test_get_fitzpatrick_modifier("👋🏻", 2);
  test_get_fitzpatrick_modifier("👋🏼", 3);
  test_get_fitzpatrick_modifier("👋🏽", 4);
  test_get_fitzpatrick_modifier("👋🏾", 5);
  test_get_fitzpatrick_modifier("👋🏿", 6);
  test_get_fitzpatrick_modifier("🏻", 2);
  test_get_fitzpatrick_modifier("🏼", 3);
  test_get_fitzpatrick_modifier("🏽", 4);
  test_get_fitzpatrick_modifier("🏾", 5);
  test_get_fitzpatrick_modifier("🏿", 6);
  test_get_fitzpatrick_modifier("⌚", 0);
  test_get_fitzpatrick_modifier("↔", 0);
  test_get_fitzpatrick_modifier("🪗", 0);
  test_get_fitzpatrick_modifier("2️⃣", 0);
  test_get_fitzpatrick_modifier("2⃣", 0);
  test_get_fitzpatrick_modifier("❤️", 0);
  test_get_fitzpatrick_modifier("❤", 0);
  test_get_fitzpatrick_modifier("⌚", 0);
  test_get_fitzpatrick_modifier("🎄", 0);
  test_get_fitzpatrick_modifier("🧑‍🎄", 0);
}

static void test_remove_emoji_modifiers(td::string emoji, const td::string &result, bool remove_selectors = true) {
  ASSERT_STREQ(result, td::remove_emoji_modifiers(emoji, remove_selectors));
  td::remove_emoji_modifiers_in_place(emoji, remove_selectors);
  ASSERT_STREQ(result, emoji);
  ASSERT_STREQ(emoji, td::remove_emoji_modifiers(emoji, remove_selectors));
}

TEST(Emoji, remove_emoji_modifiers) {
  test_remove_emoji_modifiers("", "");
  test_remove_emoji_modifiers("👩🏼‍❤‍💋‍👩🏻", "👩‍❤‍💋‍👩");
  test_remove_emoji_modifiers("👩🏼‍❤️‍💋‍👩🏻", "👩‍❤‍💋‍👩");
  test_remove_emoji_modifiers("👩🏼‍❤️‍💋‍👩🏻", "👩‍❤️‍💋‍👩", false);
  test_remove_emoji_modifiers("👋🏻", "👋");
  test_remove_emoji_modifiers("👋🏼", "👋");
  test_remove_emoji_modifiers("👋🏽", "👋");
  test_remove_emoji_modifiers("👋🏾", "👋");
  test_remove_emoji_modifiers("👋🏿", "👋");
  test_remove_emoji_modifiers("🏻", "🏻");
  test_remove_emoji_modifiers("🏼", "🏼");
  test_remove_emoji_modifiers("🏽", "🏽");
  test_remove_emoji_modifiers("🏾", "🏾");
  test_remove_emoji_modifiers("🏿", "🏿");
  test_remove_emoji_modifiers("⌚", "⌚");
  test_remove_emoji_modifiers("↔", "↔");
  test_remove_emoji_modifiers("🪗", "🪗");
  test_remove_emoji_modifiers("2️⃣", "2⃣");
  test_remove_emoji_modifiers("2⃣", "2⃣");
  test_remove_emoji_modifiers("❤️", "❤");
  test_remove_emoji_modifiers("❤", "❤");
  test_remove_emoji_modifiers("⌚", "⌚");
  test_remove_emoji_modifiers("️", "️");
  test_remove_emoji_modifiers("️️️🏻", "️️️🏻");
  test_remove_emoji_modifiers("️️️🏻a", "a");
  test_remove_emoji_modifiers("🎄", "🎄");
  test_remove_emoji_modifiers("🧑‍🎄", "🧑‍🎄");
}

static void test_remove_emoji_selectors(td::string emoji, const td::string &result) {
  ASSERT_STREQ(result, td::remove_emoji_selectors(result));
  ASSERT_STREQ(result, td::remove_emoji_selectors(emoji));
}

TEST(Emoji, remove_emoji_selectors) {
  test_remove_emoji_selectors("", "");
  test_remove_emoji_selectors("👩🏼‍❤‍💋‍👩🏻", "👩🏼‍❤‍💋‍👩🏻");
  test_remove_emoji_selectors("👩🏼‍❤️‍💋‍👩🏻", "👩🏼‍❤‍💋‍👩🏻");
  test_remove_emoji_selectors("👋🏻", "👋🏻");
  test_remove_emoji_selectors("👋🏼", "👋🏼");
  test_remove_emoji_selectors("👋🏽", "👋🏽");
  test_remove_emoji_selectors("👋🏾", "👋🏾");
  test_remove_emoji_selectors("👋🏿", "👋🏿");
  test_remove_emoji_selectors("🏻", "🏻");
  test_remove_emoji_selectors("🏼", "🏼");
  test_remove_emoji_selectors("🏽", "🏽");
  test_remove_emoji_selectors("🏾", "🏾");
  test_remove_emoji_selectors("🏿", "🏿");
  test_remove_emoji_selectors("⌚", "⌚");
  test_remove_emoji_selectors("↔", "↔");
  test_remove_emoji_selectors("🪗", "🪗");
  test_remove_emoji_selectors("2️⃣", "2⃣");
  test_remove_emoji_selectors("2⃣", "2⃣");
  test_remove_emoji_selectors("❤️", "❤");
  test_remove_emoji_selectors("❤", "❤");
  test_remove_emoji_selectors("⌚", "⌚");
  test_remove_emoji_selectors("🎄", "🎄");
  test_remove_emoji_selectors("🧑‍🎄", "🧑‍🎄");
}

static void test_find_emojis(td::Slice str, const td::vector<td::string> &expected) {
  auto emojis = td::find_emojis(str);
  td::vector<td::string> result;
  const char *last_end = str.begin();
  for (auto emoji : emojis) {
    ASSERT_TRUE(last_end <= emoji.begin());
    ASSERT_TRUE(emoji.end() <= str.end());
    ASSERT_TRUE(!emoji.empty());
    ASSERT_TRUE(td::is_emoji(emoji));
    last_end = emoji.end();
    result.push_back(emoji.str());
  }
  ASSERT_EQ(expected, result);
}

TEST(Emoji, find_emojis) {
  test_find_emojis("", {});
  test_find_emojis(" ", {});
  test_find_emojis("abc 123 #*", {});
  test_find_emojis("️", {});
  test_find_emojis("‍", {});
  test_find_emojis("👋", {"👋"});
  test_find_emojis("a👋b", {"👋"});
  test_find_emojis("👋👋", {"👋", "👋"});
  test_find_emojis("👋🏻👋🏿", {"👋🏻", "👋🏿"});
  test_find_emojis("🏻", {"🏻"});
  test_find_emojis("⌚↔🪗", {"⌚", "↔", "🪗"});
  test_find_emojis("❤️❤", {"❤️", "❤"});
  test_find_emojis("❤️️", {"❤️"});
  test_find_emojis("️❤", {"❤"});
  test_find_emojis("2️⃣3⃣4", {"2️⃣", "3⃣"});
  test_find_emojis("#️⃣*⃣", {"#️⃣", "*⃣"});
  test_find_emojis("©️ © ↔", {"©️", "©", "↔"});
  test_find_emojis("🇺🇸🇩🇪", {"🇺🇸", "🇩🇪"});
  test_find_emojis("🇦🇺🇸", {"🇦🇺"});
  test_find_emojis("🏴󠁧󠁢󠁥󠁮󠁧󠁿!", {"🏴󠁧󠁢󠁥󠁮󠁧󠁿"});
  test_find_emojis("👩🏼‍❤️‍💋‍👩🏻 text 👩🏼‍❤‍💋‍👩🏻",
                   {"👩🏼‍❤️‍💋‍👩🏻", "👩🏼‍❤‍💋‍👩🏻"});
  test_find_emojis("👩🏼‍❤‍💋‍👩🏻️", {"👩🏼‍❤‍💋‍👩🏻️"});
  test_find_emojis("👩🏼‍❤️️‍💋‍👩🏻", {"👩🏼‍❤️", "💋‍👩🏻"});
  test_find_emojis("🧑‍🎄🎄", {"🧑‍🎄", "🎄"});
  test_find_emojis("🕵️‍♂️🕵‍♂", {"🕵️‍♂️", "🕵‍♂"});
  test_find_emojis("👩‍🤝‍👨", {"👩‍🤝‍👨"});
  test_find_emojis("👩‍a‍👨", {"👩", "👨"});
  test_find_emojis("👩‍", {"👩"});
  test_find_emojis("‍👩", {"👩"});
  test_find_emojis("👩‍‍👨", {"👩", "👨"});
  test_find_emojis("👩️️‍👨", {"👩️", "👨"});
}

TEST(Emoji, find_emojis_random) {
  auto emojis =
      td::full_split(td::Slice("👋 👋🏻 🏻 ⌚ ⌚️ ↔ 🪗 ❤️ ❤ 2️⃣ 2⃣ © ©️ 🇺🇸 🇦🇺 🏴󠁧󠁢󠁥󠁮󠁧󠁿 🎄 🧑‍🎄 🕵️‍♂️ "
                               "🕵‍♂ 👩‍🤝‍👨 👩🏼‍❤️‍💋‍👩🏻"),
                     ' ');
  const td::vector<td::Slice> separators = {" ", "a", "text", "\n", "1", "#", "абв"};
  for (auto emoji : emojis) {
    ASSERT_TRUE(td::is_emoji(emoji));
  }
  for (int test = 0; test < 1000; test++) {
    td::string str;
    td::vector<td::string> expected;
    auto count = td::Random::fast(0, 20);
    for (int i = 0; i < count; i++) {
      str += separators[td::Random::fast(0, static_cast<int>(separators.size()) - 1)].str();
      auto emoji = emojis[td::Random::fast(0, static_cast<int>(emojis.size()) - 1)];
      str += emoji.str();
      expected.push_back(emoji.str());
    }
    if (td::Random::fast_bool()) {
      str += separators[td::Random::fast(0, static_cast<int>(separators.size()) - 1)].str();
    }
    test_find_emojis(str, expected);
  }
}
