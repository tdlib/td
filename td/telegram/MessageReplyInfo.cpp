//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageReplyInfo.h"

#include "td/utils/logging.h"

namespace td {

MessageReplyInfo::MessageReplyInfo(tl_object_ptr<telegram_api::messageReplies> &&reply_info, bool is_bot) {
  if (reply_info == nullptr) {
    return;
  }
  if (reply_info->replies_ < 0) {
    LOG(ERROR) << "Receive wrong " << to_string(reply_info);
    return;
  }
  reply_count = reply_info->replies_;
  pts = reply_info->replies_pts_;

  if (!is_bot) {
    for (auto &peer : reply_info->recent_repliers_) {
      DialogId dialog_id(peer);
      if (dialog_id.is_valid()) {
        recent_replier_dialog_ids.push_back(dialog_id);
      } else {
        LOG(ERROR) << "Receive " << dialog_id << " as a recent replier";
      }
    }
  }

  is_comment = reply_info->comments_;
  if (is_comment) {
    channel_id = ChannelId(reply_info->channel_id_);
    if (!channel_id.is_valid()) {
      LOG(ERROR) << "Receive invalid " << channel_id;
      channel_id = ChannelId();
    }
  }
}

bool MessageReplyInfo::need_update_to(const MessageReplyInfo &other) const {
  if (other.pts < pts) {
    return false;
  }
  return reply_count != other.reply_count || recent_replier_dialog_ids != other.recent_replier_dialog_ids;
}

void MessageReplyInfo::add_reply(DialogId replier_dialog_id) {
  CHECK(!is_empty());

  reply_count++;
  if (replier_dialog_id.is_valid() &&
      (recent_replier_dialog_ids.empty() || recent_replier_dialog_ids[0] != replier_dialog_id)) {
    td::remove(recent_replier_dialog_ids, replier_dialog_id);
    recent_replier_dialog_ids.insert(recent_replier_dialog_ids.begin(), replier_dialog_id);
    if (recent_replier_dialog_ids.size() > 3) {
      recent_replier_dialog_ids.pop_back();
    }
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageReplyInfo &reply_info) {
  return string_builder << reply_info.reply_count << " replies by " << reply_info.recent_replier_dialog_ids;
}

}  // namespace td