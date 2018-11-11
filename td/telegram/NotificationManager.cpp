//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

namespace td {

int VERBOSITY_NAME(notifications) = VERBOSITY_NAME(WARNING);

NotificationManager::NotificationManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void NotificationManager::start_up() {
  current_notification_id_ =
      NotificationId(to_integer<int32>(G()->td_db()->get_binlog_pmc()->get("notification_id_current")));
  current_notification_group_id_ =
      NotificationGroupId(to_integer<int32>(G()->td_db()->get_binlog_pmc()->get("notification_group_id_current")));
}

void NotificationManager::tear_down() {
  parent_.reset();
}

NotificationId NotificationManager::get_next_notification_id() {
  current_notification_id_ = NotificationId(current_notification_id_.get() % 0x7FFFFFFF + 1);
  G()->td_db()->get_binlog_pmc()->set("notification_id_current", to_string(current_notification_id_.get()));
  return current_notification_id_;
}

NotificationGroupId NotificationManager::get_next_notification_group_id() {
  current_notification_group_id_ = NotificationGroupId(current_notification_group_id_.get() % 0x7FFFFFFF + 1);
  G()->td_db()->get_binlog_pmc()->set("notification_group_id_current", to_string(current_notification_group_id_.get()));
  return current_notification_group_id_;
}

void NotificationManager::add_notification(NotificationGroupId group_id, DialogId dialog_id,
                                           DialogId notification_settings_dialog_id, bool silent,
                                           NotificationId notification_id, unique_ptr<NotificationType> type) {
  CHECK(type != nullptr);
  VLOG(notifications) << "Add " << notification_id << " to " << group_id << " in " << dialog_id
                      << " with settings from " << notification_settings_dialog_id
                      << (silent ? " silent" : " with sound") << ": " << *type;
  // TODO total_count++;
}

void NotificationManager::edit_notification(NotificationId notification_id, unique_ptr<NotificationType> type) {
  VLOG(notifications) << "Edit " << notification_id << ": " << *type;
}

void NotificationManager::delete_notification(NotificationId notification_id) {
}

void NotificationManager::remove_notification(NotificationId notification_id, Promise<Unit> &&promise) {
  if (!notification_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }

  // TODO update total_count
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

  // TODO update total_count
  promise.set_value(Unit());
}

}  // namespace td
