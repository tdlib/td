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

void NotificationManager::remove_notification(int32 notification_id, Promise<Unit> &&promise) {
  if (!is_valid_notification_id(notification_id)) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }
  promise.set_value(Unit());
}

void NotificationManager::remove_notifications(int32 group_id, int32 max_notification_id, Promise<Unit> &&promise) {
  if (!is_valid_group_id(group_id)) {
    return promise.set_error(Status::Error(400, "Group identifier is invalid"));
  }
  if (!is_valid_notification_id(max_notification_id)) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }
  promise.set_value(Unit());
}

bool NotificationManager::is_valid_notification_id(int32 notification_id) {
  return notification_id > 0;
}

bool NotificationManager::is_valid_group_id(int32 group_id) {
  return group_id > 0;
}

}  // namespace td
