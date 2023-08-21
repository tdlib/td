//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationGroupInfo.h"

#include "td/utils/logging.h"

namespace td {

void NotificationGroupInfo::try_reuse() {
  CHECK(group_id.is_valid());
  CHECK(last_notification_date == 0);
  if (!try_reuse_) {
    try_reuse_ = true;
    is_changed = true;
  }
}

void NotificationGroupInfo::add_group_key_if_changed(vector<NotificationGroupKey> &group_keys, DialogId dialog_id) {
  if (!is_changed) {
    return;
  }
  is_changed = false;

  group_keys.emplace_back(group_id, try_reuse_ ? DialogId() : dialog_id, last_notification_date);
}

NotificationGroupId NotificationGroupInfo::get_reused_group_id() {
  if (!try_reuse_) {
    return {};
  }
  if (is_changed) {
    LOG(ERROR) << "Failed to reuse changed " << group_id;
    return {};
  }
  try_reuse_ = false;
  if (!group_id.is_valid()) {
    LOG(ERROR) << "Failed to reuse invalid " << group_id;
    return {};
  }
  CHECK(last_notification_id == NotificationId());
  CHECK(last_notification_date == 0);
  auto result = group_id;
  group_id = NotificationGroupId();
  max_removed_notification_id = NotificationId();
  max_removed_message_id = MessageId();
  return result;
}

StringBuilder &operator<<(StringBuilder &string_builder, const NotificationGroupInfo &group_info) {
  return string_builder << group_info.group_id << " with last " << group_info.last_notification_id << " sent at "
                        << group_info.last_notification_date << ", max removed "
                        << group_info.max_removed_notification_id << '/' << group_info.max_removed_message_id;
}

}  // namespace td
