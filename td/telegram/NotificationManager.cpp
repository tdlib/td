//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/Global.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

namespace td {

int VERBOSITY_NAME(notifications) = VERBOSITY_NAME(WARNING);

NotificationManager::NotificationManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

bool NotificationManager::is_disabled() const {
  LOG(ERROR) << "IS DISABLED";
  return td_->auth_manager_->is_bot();
}

void NotificationManager::start_up() {
  if (is_disabled()) {
    return;
  }

  current_notification_id_ =
      NotificationId(to_integer<int32>(G()->td_db()->get_binlog_pmc()->get("notification_id_current")));
  current_notification_group_id_ =
      NotificationGroupId(to_integer<int32>(G()->td_db()->get_binlog_pmc()->get("notification_group_id_current")));

  max_notification_group_count_ =
      G()->shared_config().get_option_integer("notification_group_count_max", DEFAULT_NOTIFICATION_GROUP_COUNT_MAX);
  max_notification_group_size_ =
      G()->shared_config().get_option_integer("notification_group_size_max", DEFAULT_NOTIFICATION_GROUP_SIZE_MAX);

  // TODO load groups
}

void NotificationManager::tear_down() {
  parent_.reset();
}

NotificationId NotificationManager::get_next_notification_id() {
  if (is_disabled()) {
    return NotificationId();
  }

  current_notification_id_ = NotificationId(current_notification_id_.get() % 0x7FFFFFFF + 1);
  G()->td_db()->get_binlog_pmc()->set("notification_id_current", to_string(current_notification_id_.get()));
  return current_notification_id_;
}

NotificationGroupId NotificationManager::get_next_notification_group_id() {
  if (is_disabled()) {
    return NotificationGroupId();
  }

  current_notification_group_id_ = NotificationGroupId(current_notification_group_id_.get() % 0x7FFFFFFF + 1);
  G()->td_db()->get_binlog_pmc()->set("notification_group_id_current", to_string(current_notification_group_id_.get()));
  return current_notification_group_id_;
}

void NotificationManager::add_notification(NotificationGroupId group_id, DialogId dialog_id,
                                           DialogId notification_settings_dialog_id, bool silent,
                                           NotificationId notification_id, unique_ptr<NotificationType> type) {
  if (is_disabled()) {
    return;
  }

  CHECK(type != nullptr);
  VLOG(notifications) << "Add " << notification_id << " to " << group_id << " in " << dialog_id
                      << " with settings from " << notification_settings_dialog_id
                      << (silent ? " silent" : " with sound") << ": " << *type;
  // TODO total_count++;
}

void NotificationManager::edit_notification(NotificationId notification_id, unique_ptr<NotificationType> type) {
  if (is_disabled()) {
    return;
  }

  VLOG(notifications) << "Edit " << notification_id << ": " << *type;
}

void NotificationManager::delete_notification(NotificationId notification_id) {
  if (is_disabled()) {
    return;
  }
}

void NotificationManager::remove_notification(NotificationId notification_id, Promise<Unit> &&promise) {
  if (!notification_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }

  if (is_disabled()) {
    return promise.set_value(Unit());
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

  if (is_disabled()) {
    return promise.set_value(Unit());
  }

  // TODO update total_count
  promise.set_value(Unit());
}

}  // namespace td
