//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

#include "td/utils/misc.h"

#include <tuple>

namespace td {

int VERBOSITY_NAME(notifications) = VERBOSITY_NAME(WARNING);

NotificationManager::NotificationManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  flush_pending_notifications_timeout_.set_callback(on_flush_pending_notifications_timeout_callback);
  flush_pending_notifications_timeout_.set_callback_data(static_cast<void *>(this));
}

void NotificationManager::on_flush_pending_notifications_timeout_callback(void *notification_manager_ptr,
                                                                          int64 group_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto notification_manager = static_cast<NotificationManager *>(notification_manager_ptr);
  send_closure_later(notification_manager->actor_id(notification_manager),
                     &NotificationManager::flush_pending_notifications,
                     NotificationGroupId(narrow_cast<int32>(group_id_int)));
}

bool NotificationManager::is_disabled() const {
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

  on_notification_group_count_max_changed();
  on_notification_group_size_max_changed();

  on_online_cloud_timeout_changed();
  on_notification_cloud_delay_changed();
  on_notification_default_delay_changed();

  // TODO load groups
}

void NotificationManager::tear_down() {
  parent_.reset();
}

NotificationManager::NotificationGroups::iterator NotificationManager::get_group(NotificationGroupId group_id) {
  // TODO optimize
  for (auto it = groups_.begin(); it != groups_.end(); ++it) {
    if (it->first.group_id == group_id) {
      return it;
    }
  }
  return groups_.end();
}

NotificationId NotificationManager::get_max_notification_id() const {
  return current_notification_id_;
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

NotificationManager::NotificationGroupKey NotificationManager::get_last_updated_group_key() const {
  int left = max_notification_group_count_;
  auto it = groups_.begin();
  while (it != groups_.end() && left > 1) {
    ++it;
    left--;
  }
  if (it == groups_.end()) {
    return NotificationGroupKey();
  }
  return it->first;
}

int32 NotificationManager::get_notification_delay_ms(DialogId dialog_id,
                                                     const PendingNotification &notification) const {
  auto delay_ms = [&]() {
    if (dialog_id.get_type() == DialogType::SecretChat) {
      return 0;  // there is no reason to delay notifications in secret chats
    }
    if (!notification.type->can_be_delayed()) {
      return 0;
    }

    auto online_info = td_->contacts_manager_->get_my_online_status();
    if (!online_info.is_online_local && online_info.is_online_remote) {
      // If we are offline, but online from some other client then delay notification
      // for 'notification_cloud_delay' seconds.
      return notification_cloud_delay_ms_;
    }

    if (!online_info.is_online_local &&
        online_info.was_online_remote > max(static_cast<double>(online_info.was_online_local),
                                            G()->server_time_cached() - online_cloud_timeout_ms_ * 1e-3)) {
      // If we are offline, but was online from some other client in last 'online_cloud_timeout' seconds
      // after we had gone offline, then delay notification for 'notification_cloud_delay' seconds.
      return notification_cloud_delay_ms_;
    }

    if (online_info.is_online_remote) {
      // If some other client is online, then delay notification for 'notification_default_delay' seconds.
      return notification_default_delay_ms_;
    }

    // otherwise send update without additional delay
    return 0;
  }();

  auto passed_time_ms = max(0, static_cast<int32>((G()->server_time_cached() - notification.date - 1) * 1000));
  return max(delay_ms - passed_time_ms, MIN_NOTIFICATION_DELAY_MS);
}

void NotificationManager::add_notification(NotificationGroupId group_id, DialogId dialog_id, int32 date,
                                           DialogId notification_settings_dialog_id, bool is_silent,
                                           NotificationId notification_id, unique_ptr<NotificationType> type) {
  if (is_disabled()) {
    return;
  }

  CHECK(group_id.is_valid());
  CHECK(dialog_id.is_valid());
  CHECK(notification_settings_dialog_id.is_valid());
  CHECK(notification_id.is_valid());
  CHECK(type != nullptr);
  VLOG(notifications) << "Add " << notification_id << " to " << group_id << " in " << dialog_id
                      << " with settings from " << notification_settings_dialog_id
                      << (is_silent ? " silent" : " with sound") << ": " << *type;

  auto group_it = get_group(group_id);
  if (group_it == groups_.end()) {
    NotificationGroupKey group_key;
    group_key.group_id = group_id;
    group_key.dialog_id = dialog_id;
    group_key.last_notification_date = 0;
    group_it = std::move(groups_.emplace(group_key, NotificationGroup()).first);

    // TODO synchronously load old group notifications from the database
  }

  PendingNotification notification;
  notification.date = date;
  notification.settings_dialog_id = notification_settings_dialog_id;
  notification.is_silent = is_silent;
  notification.notification_id = notification_id;
  notification.type = std::move(type);

  auto delay_ms = get_notification_delay_ms(dialog_id, notification);
  VLOG(notifications) << "Delay " << notification_id << " for " << delay_ms << " milliseconds";
  auto flush_time = delay_ms * 0.001 + Time::now_cached();

  NotificationGroup &group = group_it->second;
  if (group.pending_notifications_flush_time == 0 || flush_time < group.pending_notifications_flush_time) {
    group.pending_notifications_flush_time = flush_time;
    flush_pending_notifications_timeout_.set_timeout_at(group_id.get(), group.pending_notifications_flush_time);
  }
  group.pending_notifications.push_back(std::move(notification));
}

td_api::object_ptr<td_api::notification> NotificationManager::get_notification_object(
    DialogId dialog_id, const Notification &notification) {
  return td_api::make_object<td_api::notification>(notification.notification_id.get(),
                                                   notification.type->get_notification_type_object(dialog_id));
}

void NotificationManager::send_update_notification_group(td_api::object_ptr<td_api::updateNotificationGroup> update) {
  // TODO delay and combine updates while getDifference is running
  VLOG(notifications) << "Send " << to_string(update);
  send_closure(G()->td(), &Td::send_update, std::move(update));
}

void NotificationManager::send_update_notification(NotificationGroupId notification_group_id, DialogId dialog_id,
                                                   const Notification &notification) {
  auto notification_object = get_notification_object(dialog_id, notification);
  if (notification_object->type_ == nullptr) {
    return;
  }

  // TODO delay and combine updates while getDifference is running
  auto update =
      td_api::make_object<td_api::updateNotification>(notification_group_id.get(), std::move(notification_object));
  VLOG(notifications) << "Send " << to_string(update);
  send_closure(G()->td(), &Td::send_update, std::move(update));
}

void NotificationManager::do_flush_pending_notifications(NotificationGroupKey &group_key, NotificationGroup &group,
                                                         vector<PendingNotification> &pending_notifications) {
  if (pending_notifications.empty()) {
    return;
  }

  VLOG(notifications) << "Flush " << pending_notifications.size() << " pending notifications in " << group_key
                      << " with known " << group.notifications.size() << " from total of " << group.total_count
                      << " notifications";

  size_t old_notification_count = group.notifications.size();
  size_t shown_notification_count = min(old_notification_count, max_notification_group_size_);

  vector<td_api::object_ptr<td_api::notification>> added_notifications;
  added_notifications.reserve(pending_notifications.size());
  for (auto &pending_notification : pending_notifications) {
    Notification notification{pending_notification.notification_id, pending_notification.date,
                              std::move(pending_notification.type)};
    added_notifications.push_back(get_notification_object(group_key.dialog_id, notification));
    if (added_notifications.back()->type_ == nullptr) {
      added_notifications.pop_back();
    } else {
      group.notifications.push_back(std::move(notification));
    }
  }
  group.total_count += narrow_cast<int32>(added_notifications.size());
  if (added_notifications.size() > max_notification_group_size_) {
    added_notifications.erase(
        added_notifications.begin(),
        added_notifications.begin() + (added_notifications.size() - max_notification_group_size_));
  }

  vector<int32> removed_notification_ids;
  if (shown_notification_count + added_notifications.size() > max_notification_group_size_) {
    auto removed_notification_count =
        shown_notification_count + added_notifications.size() - max_notification_group_size_;
    removed_notification_ids.reserve(removed_notification_count);
    for (size_t i = 0; i < removed_notification_count; i++) {
      removed_notification_ids.push_back(
          group.notifications[old_notification_count - shown_notification_count + i].notification_id.get());
    }
  }

  if (!added_notifications.empty()) {
    send_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
        group_key.group_id.get(), group_key.dialog_id.get(), pending_notifications[0].settings_dialog_id.get(),
        pending_notifications[0].is_silent, group.total_count, std::move(added_notifications),
        std::move(removed_notification_ids)));
  } else {
    CHECK(removed_notification_ids.empty());
  }
  pending_notifications.clear();
}

void NotificationManager::send_remove_group_update(const NotificationGroupKey &group_key,
                                                   const NotificationGroup &group,
                                                   vector<int32> &&removed_notification_ids) {
  VLOG(notifications) << "Remove " << group_key.group_id;
  auto total_size = group.notifications.size();
  CHECK(removed_notification_ids.size() <= max_notification_group_size_);
  auto removed_size = min(total_size, max_notification_group_size_ - removed_notification_ids.size());
  removed_notification_ids.reserve(removed_size + removed_notification_ids.size());
  for (size_t i = total_size - removed_size; i < total_size; i++) {
    removed_notification_ids.push_back(group.notifications[i].notification_id.get());
  }

  if (!removed_notification_ids.empty()) {
    send_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
        group_key.group_id.get(), group_key.dialog_id.get(), group_key.dialog_id.get(), true, 0,
        vector<td_api::object_ptr<td_api::notification>>(), std::move(removed_notification_ids)));
  }
}

void NotificationManager::send_add_group_update(const NotificationGroupKey &group_key, const NotificationGroup &group) {
  VLOG(notifications) << "Add " << group_key.group_id;
  auto total_size = group.notifications.size();
  auto added_size = min(total_size, max_notification_group_size_);
  vector<td_api::object_ptr<td_api::notification>> added_notifications;
  added_notifications.reserve(added_size);
  for (size_t i = total_size - added_size; i < total_size; i++) {
    added_notifications.push_back(get_notification_object(group_key.dialog_id, group.notifications[i]));
    if (added_notifications.back()->type_ == nullptr) {
      added_notifications.pop_back();
    }
  }

  if (!added_notifications.empty()) {
    send_update_notification_group(
        td_api::make_object<td_api::updateNotificationGroup>(group_key.group_id.get(), group_key.dialog_id.get(), 0,
                                                             true, 0, std::move(added_notifications), vector<int32>()));
  }
}

void NotificationManager::flush_pending_notifications(NotificationGroupId group_id) {
  auto group_it = get_group(group_id);
  CHECK(group_it != groups_.end());

  if (group_it->second.pending_notifications.empty()) {
    return;
  }

  auto group_key = group_it->first;
  auto group = std::move(group_it->second);

  groups_.erase(group_it);

  auto final_group_key = group_key;
  for (auto &pending_notification : group.pending_notifications) {
    if (pending_notification.date >= final_group_key.last_notification_date) {
      final_group_key.last_notification_date = pending_notification.date;
    }
  }
  CHECK(final_group_key.last_notification_date != 0);

  VLOG(notifications) << "Flush pending notifications in " << group_key << " up to "
                      << final_group_key.last_notification_date;

  auto last_group_key = get_last_updated_group_key();
  bool was_updated = group_key.last_notification_date != 0 && group_key < last_group_key;
  bool is_updated = final_group_key < last_group_key;

  if (!is_updated) {
    CHECK(!was_updated);
    VLOG(notifications) << "There is no need to send updateNotificationGroup in " << group_key
                        << ", because of newer notification groups";
    for (auto &pending_notification : group.pending_notifications) {
      group.notifications.emplace_back(pending_notification.notification_id, pending_notification.date,
                                       std::move(pending_notification.type));
    }
  } else {
    if (!was_updated) {
      if (last_group_key.last_notification_date != 0) {
        // need to remove last notification group to not exceed max_notification_group_size_
        send_remove_group_update(last_group_key, groups_[last_group_key], vector<int32>());
      }
      send_add_group_update(group_key, group);
    }

    DialogId notification_settings_dialog_id;
    bool is_silent = false;

    // split notifications by groups with common settings
    vector<PendingNotification> grouped_notifications;
    for (auto &pending_notification : group.pending_notifications) {
      if (notification_settings_dialog_id != pending_notification.settings_dialog_id ||
          is_silent != pending_notification.is_silent) {
        do_flush_pending_notifications(group_key, group, grouped_notifications);
        notification_settings_dialog_id = pending_notification.settings_dialog_id;
        is_silent = pending_notification.is_silent;
      }
      grouped_notifications.push_back(std::move(pending_notification));
    }
    do_flush_pending_notifications(group_key, group, grouped_notifications);
  }

  group.pending_notifications_flush_time = 0;
  group.pending_notifications.clear();
  if (group.notifications.size() >
      keep_notification_group_size_ + EXTRA_GROUP_SIZE) {  // ensure that we delete a lot of messages simultaneously
    // keep only keep_notification_group_size_ last notifications in memory
    group.notifications.erase(
        group.notifications.begin(),
        group.notifications.begin() + (group.notifications.size() - keep_notification_group_size_));
  }

  groups_.emplace(std::move(final_group_key), std::move(group));
}

void NotificationManager::edit_notification(NotificationGroupId group_id, NotificationId notification_id,
                                            unique_ptr<NotificationType> type) {
  if (is_disabled()) {
    return;
  }

  CHECK(notification_id.is_valid());
  CHECK(type != nullptr);
  VLOG(notifications) << "Edit " << notification_id << ": " << *type;

  auto group_it = get_group(group_id);
  auto &group = group_it->second;
  for (size_t i = 0; i < group.notifications.size(); i++) {
    auto &notification = group.notifications[i];
    if (notification.notification_id == notification_id) {
      notification.type = std::move(type);
      if (i + max_notification_group_size_ >= group.notifications.size()) {
        send_update_notification(group_it->first.group_id, group_it->first.dialog_id, notification);
        return;
      }
    }
  }
  for (auto &notification : group.pending_notifications) {
    if (notification.notification_id == notification_id) {
      notification.type = std::move(type);
    }
  }
}

void NotificationManager::on_notifications_removed(
    NotificationGroups::iterator &&group_it, vector<td_api::object_ptr<td_api::notification>> &&added_notifications,
    vector<int32> &&removed_notification_ids) {
  auto group_key = group_it->first;
  auto final_group_key = group_key;
  final_group_key.last_notification_date = 0;
  for (auto &notification : group_it->second.notifications) {
    if (notification.date > final_group_key.last_notification_date) {
      final_group_key.last_notification_date = notification.date;
    }
  }

  bool is_position_changed = final_group_key.last_notification_date != group_key.last_notification_date;

  NotificationGroup group = std::move(group_it->second);
  if (is_position_changed) {
    VLOG(notifications) << "Position of notification group is changed from " << group_key << " to " << final_group_key;
    groups_.erase(group_it);
  }

  auto last_group_key = get_last_updated_group_key();
  bool was_updated = false;
  bool is_updated = false;
  if (is_position_changed) {
    was_updated = group_key.last_notification_date != 0 && group_key < last_group_key;
    is_updated = final_group_key.last_notification_date != 0 && final_group_key < last_group_key;
  } else {
    was_updated = is_updated = !(last_group_key < group_key);
  }

  if (!was_updated) {
    CHECK(!is_updated);
    VLOG(notifications) << "There is no need to send updateNotificationGroup about " << group_key.group_id;
  } else {
    if (is_updated) {
      // group is still visible
      send_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
          group_key.group_id.get(), group_key.dialog_id.get(), 0, true, group.total_count,
          std::move(added_notifications), std::move(removed_notification_ids)));
    } else {
      // group needs to be removed
      send_remove_group_update(group_key, group, std::move(removed_notification_ids));
      if (last_group_key.last_notification_date != 0) {
        // need to add new last notification group
        send_add_group_update(last_group_key, groups_[last_group_key]);
      }
    }
  }

  if (is_position_changed) {
    groups_.emplace(std::move(final_group_key), std::move(group));

    last_group_key = get_last_updated_group_key();
  } else {
    group_it->second = std::move(group);
  }

  /*
  if (last_loaded_group_key_ < last_group_key) {
    // TODO load new groups from database
  }
  */
}

void NotificationManager::remove_notification(NotificationGroupId group_id, NotificationId notification_id,
                                              bool is_permanent, Promise<Unit> &&promise) {
  if (!notification_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }

  if (is_disabled()) {
    return promise.set_value(Unit());
  }

  VLOG(notifications) << "Remove " << notification_id << " from " << group_id;

  // TODO remove notification from database by notification_id

  auto group_it = get_group(group_id);
  if (group_it == groups_.end()) {
    // TODO synchronously load the group
    return promise.set_value(Unit());
  }

  for (auto it = group_it->second.pending_notifications.begin(); it != group_it->second.pending_notifications.end();
       ++it) {
    if (it->notification_id == notification_id) {
      // notification is still pending, just delete it
      group_it->second.pending_notifications.erase(it);
      if (group_it->second.pending_notifications.empty()) {
        flush_pending_notifications_timeout_.cancel_timeout(group_id.get());
      }
      return promise.set_value(Unit());
    }
  }

  bool is_found = false;
  auto old_group_size = group_it->second.notifications.size();
  size_t notification_pos = old_group_size;
  for (size_t pos = 0; pos < notification_pos; pos++) {
    if (group_it->second.notifications[pos].notification_id == notification_id) {
      notification_pos = pos;
      is_found = true;
    }
  }

  vector<td_api::object_ptr<td_api::notification>> added_notifications;
  vector<int32> removed_notification_ids;
  if (is_found && notification_pos + max_notification_group_size_ >= old_group_size) {
    removed_notification_ids.push_back(notification_id.get());
    if (old_group_size >= max_notification_group_size_ + 1) {
      added_notifications.push_back(
          get_notification_object(group_it->first.dialog_id,
                                  group_it->second.notifications[old_group_size - max_notification_group_size_ - 1]));
      if (added_notifications.back()->type_ == nullptr) {
        added_notifications.pop_back();
      }
    } else {
      // TODO preload more notifications in the group
    }
  }

  if (is_permanent) {
    group_it->second.total_count--;
  }
  if (is_found) {
    group_it->second.notifications.erase(group_it->second.notifications.begin() + notification_pos);
  }

  if (is_permanent || !removed_notification_ids.empty()) {
    on_notifications_removed(std::move(group_it), std::move(added_notifications), std::move(removed_notification_ids));
  }

  promise.set_value(Unit());
}

void NotificationManager::remove_notification_group(NotificationGroupId group_id, NotificationId max_notification_id,
                                                    MessageId max_message_id, int32 new_total_count,
                                                    Promise<Unit> &&promise) {
  if (!group_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Group identifier is invalid"));
  }
  if (!max_notification_id.is_valid() && !max_message_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }

  if (is_disabled()) {
    return promise.set_value(Unit());
  }

  VLOG(notifications) << "Remove " << group_id << " up to " << max_notification_id << " or " << max_message_id;

  if (max_notification_id.is_valid()) {
    // TODO remove notifications from database by max_notification_id, save that they are removed
  }

  auto group_it = get_group(group_id);
  if (group_it == groups_.end()) {
    // TODO synchronously load the group
    return promise.set_value(Unit());
  }

  auto pending_delete_end = group_it->second.pending_notifications.begin();
  for (auto it = group_it->second.pending_notifications.begin(); it != group_it->second.pending_notifications.end();
       ++it) {
    if (it->notification_id.get() <= max_notification_id.get() ||
        (max_message_id.is_valid() && it->type->get_message_id().get() <= max_message_id.get())) {
      pending_delete_end = it + 1;
    }
  }
  group_it->second.pending_notifications.erase(group_it->second.pending_notifications.begin(), pending_delete_end);
  if (group_it->second.pending_notifications.empty()) {
    flush_pending_notifications_timeout_.cancel_timeout(group_id.get());
  }
  if (new_total_count != -1) {
    new_total_count -= static_cast<int32>(group_it->second.pending_notifications.size());
    if (new_total_count < 0) {
      LOG(ERROR) << "Have wrong new_total_count " << new_total_count;
    }
  }

  auto old_group_size = group_it->second.notifications.size();
  auto notification_delete_end = old_group_size;
  for (size_t pos = 0; pos < notification_delete_end; pos++) {
    auto &notification = group_it->second.notifications[pos];
    if (notification.notification_id.get() > max_notification_id.get() &&
        (!max_message_id.is_valid() || notification.type->get_message_id().get() > max_message_id.get())) {
      notification_delete_end = pos;
    }
  }

  bool is_found = notification_delete_end != 0;

  vector<int32> removed_notification_ids;
  if (is_found && notification_delete_end + max_notification_group_size_ > old_group_size) {
    for (size_t i = old_group_size >= max_notification_group_size_ ? old_group_size - max_notification_group_size_ : 0;
         i < notification_delete_end; i++) {
      removed_notification_ids.push_back(group_it->second.notifications[i].notification_id.get());
    }
  }

  if (group_it->second.total_count == new_total_count) {
    new_total_count = -1;
  }
  if (new_total_count != -1) {
    group_it->second.total_count = new_total_count;
  }
  if (is_found) {
    group_it->second.notifications.erase(group_it->second.notifications.begin(),
                                         group_it->second.notifications.begin() + notification_delete_end);
  }

  if (new_total_count != -1 || !removed_notification_ids.empty()) {
    on_notifications_removed(std::move(group_it), vector<td_api::object_ptr<td_api::notification>>(),
                             std::move(removed_notification_ids));
  }
  promise.set_value(Unit());
}

void NotificationManager::on_notification_group_count_max_changed() {
  if (is_disabled()) {
    return;
  }

  auto new_max_notification_group_count =
      G()->shared_config().get_option_integer("notification_group_count_max", DEFAULT_GROUP_COUNT_MAX);
  CHECK(MIN_NOTIFICATION_GROUP_COUNT_MAX <= new_max_notification_group_count &&
        new_max_notification_group_count <= MAX_NOTIFICATION_GROUP_COUNT_MAX);

  if (static_cast<size_t>(new_max_notification_group_count) == max_notification_group_count_) {
    return;
  }

  VLOG(notifications) << "Change max notification group count from " << max_notification_group_count_ << " to "
                      << new_max_notification_group_count;

  if (max_notification_group_count_ != 0) {
    // TODO
  }

  max_notification_group_count_ = static_cast<size_t>(new_max_notification_group_count);
}

void NotificationManager::on_notification_group_size_max_changed() {
  if (is_disabled()) {
    return;
  }

  auto new_max_notification_group_size =
      G()->shared_config().get_option_integer("notification_group_size_max", DEFAULT_GROUP_SIZE_MAX);
  CHECK(MIN_NOTIFICATION_GROUP_SIZE_MAX <= new_max_notification_group_size &&
        new_max_notification_group_size <= MAX_NOTIFICATION_GROUP_SIZE_MAX);

  if (static_cast<size_t>(new_max_notification_group_size) == max_notification_group_size_) {
    return;
  }

  VLOG(notifications) << "Change max notification group size from " << max_notification_group_size_ << " to "
                      << new_max_notification_group_size;

  if (max_notification_group_size_ != 0) {
    // TODO
  }

  max_notification_group_size_ = static_cast<size_t>(new_max_notification_group_size);
  keep_notification_group_size_ =
      max_notification_group_size_ + max(EXTRA_GROUP_SIZE / 2, min(max_notification_group_size_, EXTRA_GROUP_SIZE));
}

void NotificationManager::on_online_cloud_timeout_changed() {
  online_cloud_timeout_ms_ =
      G()->shared_config().get_option_integer("online_cloud_timeout_ms", DEFAULT_ONLINE_CLOUD_TIMEOUT_MS);
  VLOG(notifications) << "Set online_cloud_timeout_ms to " << online_cloud_timeout_ms_;
}

void NotificationManager::on_notification_cloud_delay_changed() {
  notification_cloud_delay_ms_ =
      G()->shared_config().get_option_integer("notification_cloud_delay_ms", DEFAULT_ONLINE_CLOUD_DELAY_MS);
  VLOG(notifications) << "Set notification_cloud_delay_ms to " << notification_cloud_delay_ms_;
}

void NotificationManager::on_notification_default_delay_changed() {
  notification_default_delay_ms_ =
      G()->shared_config().get_option_integer("notification_default_delay_ms", DEFAULT_DEFAULT_DELAY_MS);
  VLOG(notifications) << "Set notification_default_delay_ms to " << notification_default_delay_ms_;
}

}  // namespace td
