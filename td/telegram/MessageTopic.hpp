//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageTopic.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void MessageTopic::store(StorerT &storer) const {
  bool has_type = type_ != Type::None;
  bool has_dialog_id = dialog_id_.is_valid();
  bool has_top_thread_message_id = top_thread_message_id_.is_valid();
  bool has_forum_topic_id = forum_topic_id_.is_valid();
  bool has_saved_messages_topic_id = saved_messages_topic_id_.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_type);
  STORE_FLAG(has_dialog_id);
  STORE_FLAG(has_top_thread_message_id);
  STORE_FLAG(has_forum_topic_id);
  STORE_FLAG(has_saved_messages_topic_id);
  END_STORE_FLAGS();
  if (has_type) {
    td::store(type_, storer);
  }
  if (has_dialog_id) {
    td::store(dialog_id_, storer);
  }
  if (has_top_thread_message_id) {
    td::store(top_thread_message_id_, storer);
  }
  if (has_forum_topic_id) {
    td::store(forum_topic_id_, storer);
  }
  if (has_saved_messages_topic_id) {
    td::store(saved_messages_topic_id_, storer);
  }
}

template <class ParserT>
void MessageTopic::parse(ParserT &parser) {
  bool has_type;
  bool has_dialog_id;
  bool has_top_thread_message_id;
  bool has_forum_topic_id;
  bool has_saved_messages_topic_id;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_type);
  PARSE_FLAG(has_dialog_id);
  PARSE_FLAG(has_top_thread_message_id);
  PARSE_FLAG(has_forum_topic_id);
  PARSE_FLAG(has_saved_messages_topic_id);
  END_PARSE_FLAGS();
  if (has_type) {
    td::parse(type_, parser);
  }
  if (has_dialog_id) {
    td::parse(dialog_id_, parser);
  }
  if (has_top_thread_message_id) {
    td::parse(top_thread_message_id_, parser);
  }
  if (has_forum_topic_id) {
    td::parse(forum_topic_id_, parser);
  }
  if (has_saved_messages_topic_id) {
    td::parse(saved_messages_topic_id_, parser);
  }
}

}  // namespace td
