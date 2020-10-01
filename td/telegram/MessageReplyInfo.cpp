//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageReplyInfo.h"

#include "td/telegram/ServerMessageId.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"

namespace td {

MessageReplyInfo::MessageReplyInfo(tl_object_ptr<telegram_api::messageReplies> &&reply_info, bool is_bot) {
  if (reply_info == nullptr || is_bot) {
    return;
  }
  if (reply_info->replies_ < 0) {
    LOG(ERROR) << "Receive wrong " << to_string(reply_info);
    return;
  }
  reply_count = reply_info->replies_;
  pts = reply_info->replies_pts_;

  is_comment = reply_info->comments_;

  if (is_comment) {
    channel_id = ChannelId(reply_info->channel_id_);
    if (!channel_id.is_valid()) {
      LOG(ERROR) << "Receive invalid " << channel_id;
      channel_id = ChannelId();
      is_comment = false;
    }
  }

  if (is_comment) {
    for (const auto &peer : reply_info->recent_repliers_) {
      DialogId dialog_id(peer);
      if (dialog_id.is_valid()) {
        recent_replier_dialog_ids.push_back(dialog_id);
      } else {
        LOG(ERROR) << "Receive " << dialog_id << " as a recent replier";
      }
    }
  }
  if ((reply_info->flags_ & telegram_api::messageReplies::MAX_ID_MASK) != 0 &&
      ServerMessageId(reply_info->max_id_).is_valid()) {
    max_message_id = MessageId(ServerMessageId(reply_info->max_id_));
  }
  if ((reply_info->flags_ & telegram_api::messageReplies::READ_MAX_ID_MASK) != 0 &&
      ServerMessageId(reply_info->read_max_id_).is_valid()) {
    last_read_inbox_message_id = MessageId(ServerMessageId(reply_info->read_max_id_));
  }
  if (last_read_inbox_message_id > max_message_id) {
    LOG(ERROR) << "Receive last_read_inbox_message_id = " << last_read_inbox_message_id
               << ", but max_message_id = " << max_message_id;
    max_message_id = last_read_inbox_message_id;
  }
  LOG(DEBUG) << "Parsed " << oneline(to_string(reply_info)) << " to " << *this;
}

bool MessageReplyInfo::need_update_to(const MessageReplyInfo &other) const {
  if (other.is_empty() && !is_empty()) {
    // ignore updates to empty reply info, because we will hide the info ourselves
    // return true;
  }
  if (other.pts < pts) {
    return false;
  }
  return reply_count != other.reply_count || recent_replier_dialog_ids != other.recent_replier_dialog_ids ||
         is_comment != other.is_comment || channel_id != other.channel_id;
}

bool MessageReplyInfo::update_max_message_ids(const MessageReplyInfo &other) {
  return update_max_message_ids(other.max_message_id, other.last_read_inbox_message_id,
                                other.last_read_outbox_message_id);
}

bool MessageReplyInfo::update_max_message_ids(MessageId other_max_message_id,
                                              MessageId other_last_read_inbox_message_id,
                                              MessageId other_last_read_outbox_message_id) {
  bool result = false;
  if (other_max_message_id > max_message_id) {
    max_message_id = other_max_message_id;
    result = true;
  }
  if (other_last_read_inbox_message_id > last_read_inbox_message_id) {
    last_read_inbox_message_id = other_last_read_inbox_message_id;
    result = true;
  }
  if (other_last_read_outbox_message_id > last_read_outbox_message_id) {
    last_read_outbox_message_id = other_last_read_outbox_message_id;
    result = true;
  }
  if (last_read_inbox_message_id > max_message_id) {
    max_message_id = last_read_inbox_message_id;
    result = true;
  }
  if (last_read_outbox_message_id > max_message_id) {
    max_message_id = last_read_outbox_message_id;
    result = true;
  }
  return result;
}

void MessageReplyInfo::add_reply(DialogId replier_dialog_id, MessageId reply_message_id) {
  CHECK(!is_empty());

  reply_count++;
  if (is_comment && replier_dialog_id.is_valid() &&
      (recent_replier_dialog_ids.empty() || recent_replier_dialog_ids[0] != replier_dialog_id)) {
    td::remove(recent_replier_dialog_ids, replier_dialog_id);
    recent_replier_dialog_ids.insert(recent_replier_dialog_ids.begin(), replier_dialog_id);
    if (recent_replier_dialog_ids.size() > 3) {
      recent_replier_dialog_ids.pop_back();
    }
  }

  if (reply_message_id > max_message_id) {
    max_message_id = reply_message_id;
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageReplyInfo &reply_info) {
  if (reply_info.is_comment) {
    return string_builder << reply_info.reply_count << " comments in " << reply_info.channel_id << " by "
                          << reply_info.recent_replier_dialog_ids << " read up to "
                          << reply_info.last_read_inbox_message_id << "/" << reply_info.last_read_outbox_message_id;
  } else {
    return string_builder << reply_info.reply_count << " replies read up to " << reply_info.last_read_inbox_message_id
                          << "/" << reply_info.last_read_outbox_message_id;
  }
}

}  // namespace td