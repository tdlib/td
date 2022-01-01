//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageReplyInfo.h"
#include "td/telegram/MinChannel.hpp"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void MessageReplyInfo::store(StorerT &storer) const {
  CHECK(!is_empty());
  bool has_recent_replier_dialog_ids = !recent_replier_dialog_ids.empty();
  bool has_channel_id = channel_id.is_valid();
  bool has_max_message_id = max_message_id.is_valid();
  bool has_last_read_inbox_message_id = last_read_inbox_message_id.is_valid();
  bool has_last_read_outbox_message_id = last_read_outbox_message_id.is_valid();
  bool has_replier_min_channels = !replier_min_channels.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_comment);
  STORE_FLAG(has_recent_replier_dialog_ids);
  STORE_FLAG(has_channel_id);
  STORE_FLAG(has_max_message_id);
  STORE_FLAG(has_last_read_inbox_message_id);
  STORE_FLAG(has_last_read_outbox_message_id);
  STORE_FLAG(has_replier_min_channels);
  END_STORE_FLAGS();
  td::store(reply_count, storer);
  td::store(pts, storer);
  if (has_recent_replier_dialog_ids) {
    td::store(recent_replier_dialog_ids, storer);
  }
  if (has_channel_id) {
    td::store(channel_id, storer);
  }
  if (has_max_message_id) {
    td::store(max_message_id, storer);
  }
  if (has_last_read_inbox_message_id) {
    td::store(last_read_inbox_message_id, storer);
  }
  if (has_last_read_outbox_message_id) {
    td::store(last_read_outbox_message_id, storer);
  }
  if (has_replier_min_channels) {
    td::store(replier_min_channels, storer);
  }
}

template <class ParserT>
void MessageReplyInfo::parse(ParserT &parser) {
  bool has_recent_replier_dialog_ids;
  bool has_channel_id;
  bool has_max_message_id;
  bool has_last_read_inbox_message_id;
  bool has_last_read_outbox_message_id;
  bool has_replier_min_channels;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_comment);
  PARSE_FLAG(has_recent_replier_dialog_ids);
  PARSE_FLAG(has_channel_id);
  PARSE_FLAG(has_max_message_id);
  PARSE_FLAG(has_last_read_inbox_message_id);
  PARSE_FLAG(has_last_read_outbox_message_id);
  PARSE_FLAG(has_replier_min_channels);
  END_PARSE_FLAGS();
  td::parse(reply_count, parser);
  td::parse(pts, parser);
  if (has_recent_replier_dialog_ids) {
    td::parse(recent_replier_dialog_ids, parser);
  }
  if (has_channel_id) {
    td::parse(channel_id, parser);
  }
  if (has_max_message_id) {
    td::parse(max_message_id, parser);
  }
  if (has_last_read_inbox_message_id) {
    td::parse(last_read_inbox_message_id, parser);
  }
  if (has_last_read_outbox_message_id) {
    td::parse(last_read_outbox_message_id, parser);
  }
  if (has_replier_min_channels) {
    td::parse(replier_min_channels, parser);
  }

  if (channel_id.get() == 777) {
    *this = MessageReplyInfo();
  }
  if (recent_replier_dialog_ids.size() > MAX_RECENT_REPLIERS) {
    recent_replier_dialog_ids.resize(MAX_RECENT_REPLIERS);
  }
}

}  // namespace td
