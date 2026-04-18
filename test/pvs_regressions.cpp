// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "td/telegram/ChainId.h"
#include "td/telegram/FolderId.h"
#include "td/telegram/MessageEntity.h"

#include "td/utils/Random.h"
#include "td/utils/tests.h"
#include "td/utils/utf8.h"

TEST(PvsRegressions, chain_id_folder_arithmetic_contract) {
  const auto main_chain = td::ChainId(td::FolderId::main()).get();
  const auto archive_chain = td::ChainId(td::FolderId::archive()).get();

  ASSERT_EQ((1ULL << 40), main_chain);
  ASSERT_EQ(main_chain + (1ULL << 10), archive_chain);
  ASSERT_EQ(main_chain, td::ChainId(td::FolderId(123456)).get());
}

TEST(PvsRegressions, markdown_v3_closes_entity_at_end_of_input) {
  td::FormattedText text{"A", {{td::MessageEntity::Type::Bold, 0, 1}}};
  auto markdown = td::get_markdown_v3(text);

  ASSERT_EQ("**A**", markdown.text);
  ASSERT_TRUE(markdown.entities.empty());

  auto parsed = td::parse_markdown_v3(markdown);
  ASSERT_EQ(text.text, parsed.text);
  ASSERT_EQ(text.entities, parsed.entities);
}

TEST(PvsRegressions, markdown_v3_light_fuzz_tail_closure) {
  constexpr int kIterations = 10000;
  for (int i = 0; i < kIterations; i++) {
    const int len = td::Random::fast(1, 32);
    td::string text;
    text.reserve(static_cast<size_t>(len));
    for (int j = 0; j < len; j++) {
      text.push_back(static_cast<char>('a' + td::Random::fast(0, 25)));
    }

    const int offset = td::Random::fast(0, len - 1);
    const int entity_length = td::Random::fast(1, len - offset);

    td::MessageEntity::Type type = td::MessageEntity::Type::Bold;
    switch (td::Random::fast(0, 4)) {
      case 0:
        type = td::MessageEntity::Type::Bold;
        break;
      case 1:
        type = td::MessageEntity::Type::Italic;
        break;
      case 2:
        type = td::MessageEntity::Type::Code;
        break;
      case 3:
        type = td::MessageEntity::Type::Strikethrough;
        break;
      default:
        type = td::MessageEntity::Type::Spoiler;
        break;
    }

    td::FormattedText input{text, {{type, offset, entity_length}}};
    auto markdown = td::get_markdown_v3(input);
    auto parsed = td::parse_markdown_v3(markdown);

    ASSERT_EQ(text, parsed.text);
  }
}

TEST(PvsRegressions, parse_html_precode_language_merges_deterministically) {
  td::string text = "<pre><code class=\"language-fift\">alpha</code></pre>";
  auto r_entities = td::parse_html(text);

  ASSERT_TRUE(r_entities.is_ok());
  ASSERT_EQ("alpha", text);
  ASSERT_EQ(1u, r_entities.ok().size());
  ASSERT_EQ(td::MessageEntity::Type::PreCode, r_entities.ok()[0].type);
  ASSERT_EQ(0, r_entities.ok()[0].offset);
  ASSERT_EQ(5, r_entities.ok()[0].length);
  ASSERT_EQ("fift", r_entities.ok()[0].argument);
}

TEST(PvsRegressions, parse_html_precode_adversarial_light_fuzz) {
  constexpr int kIterations = 5000;
  for (int i = 0; i < kIterations; i++) {
    const int payload_len = td::Random::fast(1, 32);
    const int lang_len = td::Random::fast(1, 8);

    td::string payload;
    payload.reserve(static_cast<size_t>(payload_len));
    for (int j = 0; j < payload_len; j++) {
      payload.push_back(static_cast<char>('a' + td::Random::fast(0, 25)));
    }

    td::string lang;
    lang.reserve(static_cast<size_t>(lang_len));
    for (int j = 0; j < lang_len; j++) {
      lang.push_back(static_cast<char>('a' + td::Random::fast(0, 25)));
    }

    td::string text = "<pre><code class=\"language-" + lang + "\">" + payload + "</code></pre>";
    auto r_entities = td::parse_html(text);

    ASSERT_TRUE(r_entities.is_ok());
    ASSERT_EQ(payload, text);
    ASSERT_EQ(1u, r_entities.ok().size());
    ASSERT_EQ(td::MessageEntity::Type::PreCode, r_entities.ok()[0].type);
    ASSERT_EQ(0, r_entities.ok()[0].offset);
    ASSERT_EQ(static_cast<td::int32>(payload.size()), r_entities.ok()[0].length);
    ASSERT_EQ(lang, r_entities.ok()[0].argument);
  }
}

TEST(PvsRegressions, parse_html_rejects_unterminated_pre_code_block) {
  td::string text = "<pre><code class=\"language-cpp\">int x = 1;";
  auto r_entities = td::parse_html(text);

  ASSERT_TRUE(r_entities.is_error());
}

TEST(PvsRegressions, parse_html_rejects_embedded_nul_bytes) {
  td::string text = "<b>a";
  text.push_back('\0');
  text += "b</b>";

  auto r_entities = td::parse_html(text);
  ASSERT_TRUE(r_entities.is_error());
}

TEST(PvsRegressions, parse_html_mixed_binary_noise_is_memory_safe) {
  constexpr int kIterations = 12000;
  const td::string utf8_samples[] = {"a", "z", "0", "_", " ", "\n", "\xC2\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80"};
  constexpr int kUtf8SampleCount = 9;
  for (int i = 0; i < kIterations; i++) {
    td::string payload;
    const int payload_len = td::Random::fast(0, 96);
    payload.reserve(static_cast<size_t>(payload_len * 4));
    for (int j = 0; j < payload_len; j++) {
      payload += utf8_samples[td::Random::fast(0, kUtf8SampleCount - 1)];
    }

    td::string html;
    switch (i % 5) {
      case 0:
        html = "<b>" + payload + "</b>";
        break;
      case 1:
        html = "<pre><code class=\"language-x\">" + payload + "</code></pre>";
        break;
      case 2:
        html = "<i>" + payload + "";
        break;
      case 3:
        html = "<a href=\"https://example.org\">" + payload + "</a>";
        break;
      default:
        html = payload;
        break;
    }

    auto r_entities = td::parse_html(html);
    if (r_entities.is_error()) {
      continue;
    }

    const auto &entities = r_entities.ok();
    auto utf16_length = static_cast<td::int64>(td::utf8_utf16_length(html));
    for (const auto &entity : entities) {
      ASSERT_TRUE(entity.offset >= 0);
      ASSERT_TRUE(entity.length >= 0);
      auto end = static_cast<td::int64>(entity.offset) + static_cast<td::int64>(entity.length);
      ASSERT_TRUE(end <= utf16_length);
    }
  }
}
