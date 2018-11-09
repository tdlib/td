//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationManager.h"

#include "td/telegram/Td.h"

namespace td {

NotificationManager::NotificationManager(Td *td) : td_(td) {
}

NotificationId NotificationManager::get_next_notification_id() {
  return NotificationId();
}

void NotificationManager::add_notification(NotificationGroupId group_id, int32 total_count, DialogId dialog_id,
                                           DialogId notification_settings_dialog_id, bool silent,
                                           NotificationId notification_id, unique_ptr<NotificationType> type) {
}

void NotificationManager::edit_notification(NotificationId notification_id, unique_ptr<NotificationType> type) {
}

void NotificationManager::delete_notification(NotificationId notification_id) {
}

void NotificationManager::remove_notification(NotificationId notification_id, Promise<Unit> &&promise) {
  if (!notification_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }
  promise.set_value(Unit());
}

void NotificationManager::remove_notification_group(NotificationGroupId group_id, NotificationId max_notification_id,
                                                    Promise<Unit> &&promise) {
  if (!group_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Group identifier is invalid"));
  }
  if (!max_notification_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }
  promise.set_value(Unit());
}

}  // namespace td
