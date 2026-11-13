//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AiComposeStyle.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void AiComposeStyle::store(StorerT &storer) const {
  bool has_tone_name = !tone_name_.empty();
  bool has_tone_id = tone_id_ != 0;
  bool has_tone_access_hash = tone_access_hash_ != 0;
  bool has_to_language_code = !to_language_code_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_tone_name);
  STORE_FLAG(has_tone_id);
  STORE_FLAG(has_tone_access_hash);
  STORE_FLAG(has_to_language_code);
  STORE_FLAG(emojify_);
  STORE_FLAG(proofread_);
  END_STORE_FLAGS();
  td::store(tone_type_, storer);
  if (has_tone_name) {
    td::store(tone_name_, storer);
  }
  if (has_tone_id) {
    td::store(tone_id_, storer);
  }
  if (has_tone_access_hash) {
    td::store(tone_access_hash_, storer);
  }
  if (has_to_language_code) {
    td::store(to_language_code_, storer);
  }
}

template <class ParserT>
void AiComposeStyle::parse(ParserT &parser) {
  bool has_tone_name;
  bool has_tone_id;
  bool has_tone_access_hash;
  bool has_to_language_code;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_tone_name);
  PARSE_FLAG(has_tone_id);
  PARSE_FLAG(has_tone_access_hash);
  PARSE_FLAG(has_to_language_code);
  PARSE_FLAG(emojify_);
  PARSE_FLAG(proofread_);
  END_PARSE_FLAGS();
  td::parse(tone_type_, parser);
  if (has_tone_name) {
    td::parse(tone_name_, parser);
  }
  if (has_tone_id) {
    td::parse(tone_id_, parser);
  }
  if (has_tone_access_hash) {
    td::parse(tone_access_hash_, parser);
  }
  if (has_to_language_code) {
    td::parse(to_language_code_, parser);
  }
}

}  // namespace td
