//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AiComposeTone.h"
#include "td/telegram/AiComposeToneExample.hpp"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void AiComposeTone::store(StorerT &storer) const {
  bool has_custom_emoji_id = custom_emoji_id_.is_valid();
  bool has_id = id_ != 0;
  bool has_access_hash = access_hash_ != 0;
  bool has_install_count = install_count_ != 0;
  bool has_author_user_id = author_user_id_ != UserId();
  bool has_prompt = !prompt_.empty();
  bool has_english_example = !english_example_.is_empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_creator_);
  STORE_FLAG(has_custom_emoji_id);
  STORE_FLAG(has_id);
  STORE_FLAG(has_access_hash);
  STORE_FLAG(has_install_count);
  STORE_FLAG(has_author_user_id);
  STORE_FLAG(has_prompt);
  STORE_FLAG(has_english_example);
  END_STORE_FLAGS();
  td::store(type_, storer);
  td::store(slug_, storer);
  if (has_custom_emoji_id) {
    td::store(custom_emoji_id_, storer);
  }
  td::store(title_, storer);
  if (has_id) {
    td::store(id_, storer);
  }
  if (has_access_hash) {
    td::store(access_hash_, storer);
  }
  if (has_install_count) {
    td::store(install_count_, storer);
  }
  if (has_author_user_id) {
    td::store(author_user_id_, storer);
  }
  if (has_prompt) {
    td::store(prompt_, storer);
  }
  if (has_english_example) {
    td::store(english_example_, storer);
  }
}

template <class ParserT>
void AiComposeTone::parse(ParserT &parser) {
  bool has_custom_emoji_id;
  bool has_id;
  bool has_access_hash;
  bool has_install_count;
  bool has_author_user_id;
  bool has_prompt;
  bool has_english_example;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_creator_);
  PARSE_FLAG(has_custom_emoji_id);
  PARSE_FLAG(has_id);
  PARSE_FLAG(has_access_hash);
  PARSE_FLAG(has_install_count);
  PARSE_FLAG(has_author_user_id);
  PARSE_FLAG(has_prompt);
  PARSE_FLAG(has_english_example);
  END_PARSE_FLAGS();
  td::parse(type_, parser);
  td::parse(slug_, parser);
  if (has_custom_emoji_id) {
    td::parse(custom_emoji_id_, parser);
  }
  td::parse(title_, parser);
  if (has_id) {
    td::parse(id_, parser);
  }
  if (has_access_hash) {
    td::parse(access_hash_, parser);
  }
  if (has_install_count) {
    td::parse(install_count_, parser);
  }
  if (has_author_user_id) {
    td::parse(author_user_id_, parser);
  }
  if (has_prompt) {
    td::parse(prompt_, parser);
  }
  if (has_english_example) {
    td::parse(english_example_, parser);
  }
}

template <class StorerT>
void AiComposeTones::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(tones_, storer);
  td::store(hash_, storer);
}

template <class ParserT>
void AiComposeTones::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(tones_, parser);
  td::parse(hash_, parser);
}

}  // namespace td
