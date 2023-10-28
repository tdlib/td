//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/RepliedMessageInfo.h"

#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.hpp"
#include "td/telegram/MessageOrigin.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void RepliedMessageInfo::store(StorerT &storer) const {
  bool has_message_id = message_id_.is_valid() || message_id_.is_valid_scheduled();
  bool has_dialog_id = dialog_id_.is_valid();
  bool has_origin_date = origin_date_ != 0;
  bool has_origin = !origin_.is_empty();
  bool has_quote = !quote_.text.empty();
  bool has_content = content_ != nullptr;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_message_id);
  STORE_FLAG(has_dialog_id);
  STORE_FLAG(has_origin_date);
  STORE_FLAG(has_origin);
  STORE_FLAG(has_quote);
  STORE_FLAG(is_quote_manual_);
  STORE_FLAG(has_content);
  END_STORE_FLAGS();
  if (has_message_id) {
    td::store(message_id_, storer);
  }
  if (has_dialog_id) {
    td::store(dialog_id_, storer);
  }
  if (has_origin_date) {
    td::store(origin_date_, storer);
  }
  if (has_origin) {
    td::store(origin_, storer);
  }
  if (has_quote) {
    td::store(quote_, storer);
  }
  if (has_content) {
    store_message_content(content_.get(), storer);
  }
}

template <class ParserT>
void RepliedMessageInfo::parse(ParserT &parser) {
  bool has_message_id;
  bool has_dialog_id;
  bool has_origin_date;
  bool has_origin;
  bool has_quote;
  bool has_content;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_message_id);
  PARSE_FLAG(has_dialog_id);
  PARSE_FLAG(has_origin_date);
  PARSE_FLAG(has_origin);
  PARSE_FLAG(has_quote);
  PARSE_FLAG(is_quote_manual_);
  PARSE_FLAG(has_content);
  END_PARSE_FLAGS();
  if (has_message_id) {
    td::parse(message_id_, parser);
  }
  if (has_dialog_id) {
    td::parse(dialog_id_, parser);
  }
  if (has_origin_date) {
    td::parse(origin_date_, parser);
  }
  if (has_origin) {
    td::parse(origin_, parser);
  }
  if (has_quote) {
    td::parse(quote_, parser);
    remove_unallowed_quote_entities(quote_);
  }
  if (has_content) {
    parse_message_content(content_, parser);
  }
}

}  // namespace td
