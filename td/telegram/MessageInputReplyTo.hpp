//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageInputReplyTo.h"

#include "td/telegram/MessageOrigin.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void MessageInputReplyTo::store(StorerT &storer) const {
  bool has_message_id = message_id_.is_valid();
  bool has_story_full_id = story_full_id_.is_valid();
  bool has_quote = !quote_.text.empty();
  bool has_dialog_id = dialog_id_.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_message_id);
  STORE_FLAG(has_story_full_id);
  STORE_FLAG(has_quote);
  STORE_FLAG(has_dialog_id);
  END_STORE_FLAGS();
  if (has_message_id) {
    td::store(message_id_, storer);
  }
  if (has_story_full_id) {
    td::store(story_full_id_, storer);
  }
  if (has_quote) {
    td::store(quote_, storer);
  }
  if (has_dialog_id) {
    td::store(dialog_id_, storer);
  }
}

template <class ParserT>
void MessageInputReplyTo::parse(ParserT &parser) {
  bool has_message_id;
  bool has_story_full_id;
  bool has_quote;
  bool has_dialog_id;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_message_id);
  PARSE_FLAG(has_story_full_id);
  PARSE_FLAG(has_quote);
  PARSE_FLAG(has_dialog_id);
  END_PARSE_FLAGS();
  if (has_message_id) {
    td::parse(message_id_, parser);
  }
  if (has_story_full_id) {
    td::parse(story_full_id_, parser);
  }
  if (has_quote) {
    td::parse(quote_, parser);
    remove_unallowed_quote_entities(quote_);
  }
  if (has_dialog_id) {
    td::parse(dialog_id_, parser);
  }
}

}  // namespace td
