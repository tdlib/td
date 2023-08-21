//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationGroupInfo.h"

#include "td/telegram/NotificationManager.h"

#include "td/utils/logging.h"

namespace td {

bool NotificationGroupInfo::set_last_notification(int32 last_notification_date, NotificationId last_notification_id,
                                                  const char *source) {
  if (last_notification_date_ != last_notification_date || last_notification_id_ != last_notification_id) {
    VLOG(notifications) << "Set " << group_id_ << " last notification to " << last_notification_id << " sent at "
                        << last_notification_date << " from " << source;
    last_notification_date_ = last_notification_date;
    last_notification_id_ = last_notification_id;
    is_changed_ = true;
    return true;
  }
  return false;
}

void NotificationGroupInfo::try_reuse() {
  CHECK(group_id_.is_valid());
  CHECK(last_notification_date_ == 0);
  if (!try_reuse_) {
    try_reuse_ = true;
    is_changed_ = true;
  }
}

void NotificationGroupInfo::add_group_key_if_changed(vector<NotificationGroupKey> &group_keys, DialogId dialog_id) {
  if (!is_changed_) {
    return;
  }
  is_changed_ = false;

  group_keys.emplace_back(group_id_, try_reuse_ ? DialogId() : dialog_id, last_notification_date_);
}

NotificationGroupId NotificationGroupInfo::get_reused_group_id() {
  if (!try_reuse_) {
    return {};
  }
  if (is_changed_) {
    LOG(ERROR) << "Failed to reuse changed " << group_id_;
    return {};
  }
  try_reuse_ = false;
  if (!group_id_.is_valid()) {
    LOG(ERROR) << "Failed to reuse invalid " << group_id_;
    return {};
  }
  CHECK(last_notification_id_ == NotificationId());
  CHECK(last_notification_date_ == 0);
  auto result = group_id_;
  group_id_ = NotificationGroupId();
  max_removed_notification_id_ = NotificationId();
  max_removed_message_id_ = MessageId();
  return result;
}

StringBuilder &operator<<(StringBuilder &string_builder, const NotificationGroupInfo &group_info) {
  return string_builder << group_info.group_id_ << " with last " << group_info.last_notification_id_ << " sent at "
                        << group_info.last_notification_date_ << ", max removed "
                        << group_info.max_removed_notification_id_ << '/' << group_info.max_removed_message_id_;
}

}  // namespace td
