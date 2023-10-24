//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/RepliedMessageInfo.h"

#include "td/telegram/MessageOrigin.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void RepliedMessageInfo::store(StorerT &storer) const {
  bool has_reply_to_message_id = reply_to_message_id_.is_valid() || reply_to_message_id_.is_valid_scheduled();
  bool has_reply_in_dialog_id = reply_in_dialog_id_.is_valid();
  bool has_reply_date = reply_date_ != 0;
  bool has_reply_origin = !reply_origin_.is_empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_reply_to_message_id);
  STORE_FLAG(has_reply_in_dialog_id);
  STORE_FLAG(has_reply_date);
  STORE_FLAG(has_reply_origin);
  END_STORE_FLAGS();
  if (has_reply_to_message_id) {
    td::store(reply_to_message_id_, storer);
  }
  if (has_reply_in_dialog_id) {
    td::store(reply_in_dialog_id_, storer);
  }
  if (has_reply_date) {
    td::store(reply_date_, storer);
  }
  if (has_reply_origin) {
    td::store(reply_origin_, storer);
  }
}

template <class ParserT>
void RepliedMessageInfo::parse(ParserT &parser) {
  bool has_reply_to_message_id;
  bool has_reply_in_dialog_id;
  bool has_reply_date;
  bool has_reply_origin;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_reply_to_message_id);
  PARSE_FLAG(has_reply_in_dialog_id);
  PARSE_FLAG(has_reply_date);
  PARSE_FLAG(has_reply_origin);
  END_PARSE_FLAGS();
  if (has_reply_to_message_id) {
    td::parse(reply_to_message_id_, parser);
  }
  if (has_reply_in_dialog_id) {
    td::parse(reply_in_dialog_id_, parser);
  }
  if (has_reply_date) {
    td::parse(reply_date_, parser);
  }
  if (has_reply_origin) {
    td::parse(reply_origin_, parser);
  }
}

}  // namespace td
