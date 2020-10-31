//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

namespace td {

class ContactsManager;
class MessagesManager;

struct MessageReplyInfo {
  int32 reply_count = -1;
  int32 pts = -1;
  vector<DialogId> recent_replier_dialog_ids;  // comments only
  ChannelId channel_id;                        // comments only
  MessageId max_message_id;
  MessageId last_read_inbox_message_id;
  MessageId last_read_outbox_message_id;
  bool is_comment = false;

  MessageReplyInfo() = default;

  MessageReplyInfo(tl_object_ptr<telegram_api::messageReplies> &&reply_info, bool is_bot);

  bool is_empty() const {
    return reply_count < 0;
  }

  bool need_update_to(const MessageReplyInfo &other) const;

  bool update_max_message_ids(MessageId other_max_message_id, MessageId other_last_read_inbox_message_id,
                              MessageId other_last_read_outbox_message_id);

  bool update_max_message_ids(const MessageReplyInfo &other);

  bool add_reply(DialogId replier_dialog_id, MessageId reply_message_id, int diff);

  td_api::object_ptr<td_api::messageReplyInfo> get_message_reply_info_object(
      ContactsManager *contacts_manager, const MessagesManager *messages_manager) const;

  template <class StorerT>
  void store(StorerT &storer) const {
    CHECK(!is_empty());
    bool has_recent_replier_dialog_ids = !recent_replier_dialog_ids.empty();
    bool has_channel_id = channel_id.is_valid();
    bool has_max_message_id = max_message_id.is_valid();
    bool has_last_read_inbox_message_id = last_read_inbox_message_id.is_valid();
    bool has_last_read_outbox_message_id = last_read_outbox_message_id.is_valid();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_comment);
    STORE_FLAG(has_recent_replier_dialog_ids);
    STORE_FLAG(has_channel_id);
    STORE_FLAG(has_max_message_id);
    STORE_FLAG(has_last_read_inbox_message_id);
    STORE_FLAG(has_last_read_outbox_message_id);
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
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    bool has_recent_replier_dialog_ids;
    bool has_channel_id;
    bool has_max_message_id;
    bool has_last_read_inbox_message_id;
    bool has_last_read_outbox_message_id;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_comment);
    PARSE_FLAG(has_recent_replier_dialog_ids);
    PARSE_FLAG(has_channel_id);
    PARSE_FLAG(has_max_message_id);
    PARSE_FLAG(has_last_read_inbox_message_id);
    PARSE_FLAG(has_last_read_outbox_message_id);
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
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, const MessageReplyInfo &reply_info);

}  // namespace td