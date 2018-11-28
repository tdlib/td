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
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

#include "td/utils/misc.h"

#include <algorithm>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace td {

int VERBOSITY_NAME(notifications) = VERBOSITY_NAME(WARNING);

NotificationManager::NotificationManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  flush_pending_notifications_timeout_.set_callback(on_flush_pending_notifications_timeout_callback);
  flush_pending_notifications_timeout_.set_callback_data(static_cast<void *>(this));

  flush_pending_updates_timeout_.set_callback(on_flush_pending_updates_timeout_callback);
  flush_pending_updates_timeout_.set_callback_data(static_cast<void *>(this));
}

void NotificationManager::on_flush_pending_notifications_timeout_callback(void *notification_manager_ptr,
                                                                          int64 group_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto notification_manager = static_cast<NotificationManager *>(notification_manager_ptr);
  if (group_id_int > 0) {
    send_closure_later(notification_manager->actor_id(notification_manager),
                       &NotificationManager::flush_pending_notifications,
                       NotificationGroupId(narrow_cast<int32>(group_id_int)));
  } else if (group_id_int == 0) {
    send_closure_later(notification_manager->actor_id(notification_manager),
                       &NotificationManager::after_get_difference_impl);
  } else {
    send_closure_later(notification_manager->actor_id(notification_manager),
                       &NotificationManager::after_get_chat_difference_impl,
                       NotificationGroupId(narrow_cast<int32>(-group_id_int)));
  }
}

void NotificationManager::on_flush_pending_updates_timeout_callback(void *notification_manager_ptr,
                                                                    int64 group_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto notification_manager = static_cast<NotificationManager *>(notification_manager_ptr);
  send_closure_later(notification_manager->actor_id(notification_manager), &NotificationManager::flush_pending_updates,
                     narrow_cast<int32>(group_id_int), "timeout");
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

  // TODO send updateActiveNotifications
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

NotificationManager::NotificationGroups::iterator NotificationManager::get_group_force(NotificationGroupId group_id) {
  auto group_it = get_group(group_id);
  if (group_it != groups_.end()) {
    return group_it;
  }

  auto message_group = td_->messages_manager_->get_message_notification_group_force(group_id);
  if (!message_group.dialog_id.is_valid()) {
    return groups_.end();
  }

  NotificationGroupKey group_key;
  group_key.group_id = group_id;
  group_key.dialog_id = message_group.dialog_id;
  group_key.last_notification_date = 0;
  for (auto &notification : message_group.notifications) {
    if (notification.date >= group_key.last_notification_date) {
      group_key.last_notification_date = notification.date;
    }
  }

  NotificationGroup group;
  group.total_count = message_group.total_count;
  group.notifications = std::move(message_group.notifications);

  return groups_.emplace(std::move(group_key), std::move(group)).first;
}

int32 NotificationManager::get_max_notification_group_size() const {
  return max_notification_group_size_;
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
  int32 left = max_notification_group_count_;
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
                      << (is_silent ? "   silently" : " with sound") << ": " << *type;

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    NotificationGroupKey group_key;
    group_key.group_id = group_id;
    group_key.dialog_id = dialog_id;
    group_key.last_notification_date = 0;
    group_it = std::move(groups_.emplace(group_key, NotificationGroup()).first);
  }

  PendingNotification notification;
  notification.date = date;
  notification.settings_dialog_id = notification_settings_dialog_id;
  notification.is_silent = is_silent;
  notification.notification_id = notification_id;
  notification.type = std::move(type);

  auto delay_ms = get_notification_delay_ms(dialog_id, notification);
  VLOG(notifications) << "Delay " << notification_id << " for " << delay_ms << " milliseconds";
  auto flush_time = delay_ms * 0.001 + Time::now();

  NotificationGroup &group = group_it->second;
  if (group.pending_notifications_flush_time == 0 || flush_time < group.pending_notifications_flush_time) {
    group.pending_notifications_flush_time = flush_time;
    flush_pending_notifications_timeout_.set_timeout_at(group_id.get(), group.pending_notifications_flush_time);
  }
  group.pending_notifications.push_back(std::move(notification));
}

struct NotificationUpdate {
  const td_api::Update *update;
};

StringBuilder &operator<<(StringBuilder &string_builder, const NotificationUpdate &update) {
  if (update.update == nullptr) {
    return string_builder << "null";
  }
  switch (update.update->get_id()) {
    case td_api::updateNotification::ID: {
      auto p = static_cast<const td_api::updateNotification *>(update.update);
      return string_builder << "update[" << NotificationId(p->notification_->id_) << " from "
                            << NotificationGroupId(p->notification_group_id_) << ']';
    }
    case td_api::updateNotificationGroup::ID: {
      auto p = static_cast<const td_api::updateNotificationGroup *>(update.update);
      vector<int32> added_notification_ids;
      for (auto &notification : p->added_notifications_) {
        added_notification_ids.push_back(notification->id_);
      }

      return string_builder << "update[" << NotificationGroupId(p->notification_group_id_) << " from "
                            << DialogId(p->chat_id_) << " with settings from "
                            << DialogId(p->notification_settings_chat_id_)
                            << (p->is_silent_ ? "   silently" : " with sound") << "; total_count = " << p->total_count_
                            << ", add " << added_notification_ids << ", remove " << p->removed_notification_ids_;
    }
    default:
      UNREACHABLE();
      return string_builder << "unknown";
  }
}

NotificationUpdate as_notification_update(const td_api::Update *update) {
  return NotificationUpdate{update};
}

void NotificationManager::add_update(int32 group_id, td_api::object_ptr<td_api::Update> update) {
  VLOG(notifications) << "Add " << as_notification_update(update.get());
  pending_updates_[group_id].push_back(std::move(update));
  if (!running_get_difference_ && running_get_chat_difference_.count(group_id) == 0) {
    flush_pending_updates_timeout_.add_timeout_in(group_id, MIN_UPDATE_DELAY_MS * 1e-3);
  } else {
    flush_pending_updates_timeout_.set_timeout_in(group_id, MAX_UPDATE_DELAY_MS * 1e-3);
  }
}

void NotificationManager::add_update_notification_group(td_api::object_ptr<td_api::updateNotificationGroup> update) {
  auto group_id = update->notification_group_id_;
  if (update->notification_settings_chat_id_ == 0) {
    update->notification_settings_chat_id_ = update->chat_id_;
  }
  add_update(group_id, std::move(update));
}

void NotificationManager::add_update_notification(NotificationGroupId notification_group_id, DialogId dialog_id,
                                                  const Notification &notification) {
  auto notification_object = get_notification_object(dialog_id, notification);
  if (notification_object->type_ == nullptr) {
    return;
  }

  add_update(notification_group_id.get(), td_api::make_object<td_api::updateNotification>(
                                              notification_group_id.get(), std::move(notification_object)));
}

void NotificationManager::flush_pending_updates(int32 group_id, const char *source) {
  auto it = pending_updates_.find(group_id);
  if (it == pending_updates_.end()) {
    return;
  }

  auto updates = std::move(it->second);
  pending_updates_.erase(it);

  VLOG(notifications) << "Send " << updates.size() << " pending updates in " << NotificationGroupId(group_id)
                      << " from " << source;
  for (auto &update : updates) {
    VLOG(notifications) << "Have " << as_notification_update(update.get());
  }

  // if a notification was added, then deleted and then re-added we need to keep
  // first addition, because it can be with sound,
  // deletion, because number of notification should never exceed max_notification_group_size_,
  // and second addition, because we has kept the deletion

  // calculate last state of all notifications
  std::unordered_set<int32> added_notification_ids;
  std::unordered_set<int32> edited_notification_ids;
  std::unordered_set<int32> removed_notification_ids;
  for (auto &update : updates) {
    if (update == nullptr) {
      continue;
    }

    if (update->get_id() == td_api::updateNotificationGroup::ID) {
      auto update_ptr = static_cast<td_api::updateNotificationGroup *>(update.get());
      for (auto &notification : update_ptr->added_notifications_) {
        auto notification_id = notification->id_;
        bool is_inserted = added_notification_ids.insert(notification_id).second;
        CHECK(is_inserted);                                          // there must be no additions after addition
        CHECK(edited_notification_ids.count(notification_id) == 0);  // there must be no additions after edit
        removed_notification_ids.erase(notification_id);
      }
      for (auto &notification_id : update_ptr->removed_notification_ids_) {
        added_notification_ids.erase(notification_id);
        edited_notification_ids.erase(notification_id);
        bool is_inserted = removed_notification_ids.insert(notification_id).second;
        CHECK(is_inserted);  // there must be no deletions after deletions
      }
    } else {
      CHECK(update->get_id() == td_api::updateNotification::ID);
      auto update_ptr = static_cast<td_api::updateNotification *>(update.get());
      auto notification_id = update_ptr->notification_->id_;
      CHECK(removed_notification_ids.count(notification_id) == 0);  // there must be no edits of deleted notifications
      added_notification_ids.erase(notification_id);
      edited_notification_ids.insert(notification_id);
    }
  }

  // we need to keep only additions of notifications from added_notification_ids/edited_notification_ids and
  // all edits of notifications from edited_notification_ids
  // deletions of a notification can be removed, only if the addition of the notification has already been deleted
  // deletions of all unkept notifications can be moved to the first updateNotificationGroup
  // after that at every moment there is no more active notifications than in the last moment,
  // so left deletions after add/edit can be safely removed and following additions can be treated as edits
  // we still need to keep deletions coming first, because we can't have 2 consequent additions
  // from all additions of the same notification, we need to preserve the first, because it can be with sound,
  // all other additions and edits can be merged to the first addition/edit
  // i.e. in edit+delete+add chain we want to remove deletion and merge addition to the edit

  bool is_changed = true;
  while (is_changed) {
    is_changed = false;

    size_t cur_pos = 0;
    std::unordered_map<int32, size_t> first_add_notification_pos;
    std::unordered_map<int32, size_t> first_edit_notification_pos;
    std::unordered_set<int32> can_be_deleted_notification_ids;
    std::vector<int32> moved_deleted_notification_ids;
    size_t first_notification_group_pos = 0;
    for (auto &update : updates) {
      cur_pos++;
      if (update == nullptr) {
        is_changed = true;
        continue;
      }

      if (update->get_id() == td_api::updateNotificationGroup::ID) {
        auto update_ptr = static_cast<td_api::updateNotificationGroup *>(update.get());

        for (auto &notification : update_ptr->added_notifications_) {
          auto notification_id = notification->id_;
          bool is_needed =
              added_notification_ids.count(notification_id) != 0 || edited_notification_ids.count(notification_id) != 0;
          if (!is_needed) {
            VLOG(notifications) << "Remove unneeded addition of " << notification_id << " in update " << cur_pos;
            can_be_deleted_notification_ids.insert(notification_id);
            notification = nullptr;
            is_changed = true;
            continue;
          }

          auto edit_it = first_edit_notification_pos.find(notification_id);
          if (edit_it != first_edit_notification_pos.end()) {
            VLOG(notifications) << "Move addition of " << notification_id << " in update " << cur_pos
                                << " to edit in update " << edit_it->second;
            CHECK(edit_it->second < cur_pos);
            auto previous_update_ptr = static_cast<td_api::updateNotification *>(updates[edit_it->second - 1].get());
            CHECK(previous_update_ptr->notification_->id_ == notification_id);
            previous_update_ptr->notification_->type_ = std::move(notification->type_);
            is_changed = true;
            notification = nullptr;
            continue;
          }
          auto add_it = first_add_notification_pos.find(notification_id);
          if (add_it != first_add_notification_pos.end()) {
            VLOG(notifications) << "Move addition of " << notification_id << " in update " << cur_pos << " to update "
                                << add_it->second;
            CHECK(add_it->second < cur_pos);
            auto previous_update_ptr =
                static_cast<td_api::updateNotificationGroup *>(updates[add_it->second - 1].get());
            bool is_found = false;
            for (auto &prev_notification : previous_update_ptr->added_notifications_) {
              if (prev_notification->id_ == notification_id) {
                prev_notification->type_ = std::move(notification->type_);
                is_found = true;
                break;
              }
            }
            CHECK(is_found);
            is_changed = true;
            notification = nullptr;
            continue;
          }

          // it is a first addition/edit of needed notification
          first_add_notification_pos[notification_id] = cur_pos;
        }
        update_ptr->added_notifications_.erase(
            std::remove_if(update_ptr->added_notifications_.begin(), update_ptr->added_notifications_.end(),
                           [](auto &notification) { return notification == nullptr; }),
            update_ptr->added_notifications_.end());
        if (update_ptr->added_notifications_.empty() && !update_ptr->is_silent_) {
          update_ptr->is_silent_ = true;
          is_changed = true;
        }

        for (auto &notification_id : update_ptr->removed_notification_ids_) {
          bool is_needed =
              added_notification_ids.count(notification_id) != 0 || edited_notification_ids.count(notification_id) != 0;
          if (can_be_deleted_notification_ids.count(notification_id) == 1) {
            CHECK(!is_needed);
            VLOG(notifications) << "Remove unneeded deletion of " << notification_id << " in update " << cur_pos;
            notification_id = 0;
            is_changed = true;
            continue;
          }
          if (!is_needed) {
            if (first_notification_group_pos != 0) {
              VLOG(notifications) << "Need to keep deletion of " << notification_id << " in update " << cur_pos
                                  << ", but can move it to the first updateNotificationGroup at pos "
                                  << first_notification_group_pos;
              moved_deleted_notification_ids.push_back(notification_id);
              notification_id = 0;
              is_changed = true;
            }
            continue;
          }

          if (first_add_notification_pos.count(notification_id) != 0 ||
              first_edit_notification_pos.count(notification_id) != 0) {
            // the notification will be re-added, and we will be able to merge the addition with previous update, so we can just remove the deletion
            VLOG(notifications) << "Remove unneeded deletion in update " << cur_pos;
            notification_id = 0;
            is_changed = true;
            continue;
          }

          // we need to keep the deletion, because otherwise we will have 2 consequent additions
        }
        update_ptr->removed_notification_ids_.erase(
            std::remove_if(update_ptr->removed_notification_ids_.begin(), update_ptr->removed_notification_ids_.end(),
                           [](auto &notification_id) { return notification_id == 0; }),
            update_ptr->removed_notification_ids_.end());

        if (update_ptr->removed_notification_ids_.empty() && update_ptr->added_notifications_.empty()) {
          for (size_t i = cur_pos - 1; i > 0; i--) {
            if (updates[i - 1] != nullptr && updates[i - 1]->get_id() == td_api::updateNotificationGroup::ID) {
              VLOG(notifications) << "Move total_count from empty update " << cur_pos << " to update " << i;
              auto previous_update_ptr = static_cast<td_api::updateNotificationGroup *>(updates[i - 1].get());
              previous_update_ptr->total_count_ = update_ptr->total_count_;
              is_changed = true;
              update = nullptr;
              break;
            }
          }
          if (update != nullptr && (cur_pos == 1 || update_ptr->total_count_ == 0)) {
            VLOG(notifications) << "Remove empty update " << cur_pos;
            is_changed = true;
            update = nullptr;
          }
        }

        if (first_notification_group_pos == 0 && update != nullptr) {
          first_notification_group_pos = cur_pos;
        }
      } else {
        CHECK(update->get_id() == td_api::updateNotification::ID);
        auto update_ptr = static_cast<td_api::updateNotification *>(update.get());
        auto notification_id = update_ptr->notification_->id_;
        bool is_needed =
            added_notification_ids.count(notification_id) != 0 || edited_notification_ids.count(notification_id) != 0;
        if (!is_needed) {
          VLOG(notifications) << "Remove unneeded update " << cur_pos;
          is_changed = true;
          update = nullptr;
          continue;
        }
        auto edit_it = first_edit_notification_pos.find(notification_id);
        if (edit_it != first_edit_notification_pos.end()) {
          VLOG(notifications) << "Move edit of " << notification_id << " in update " << cur_pos << " to update "
                              << edit_it->second;
          CHECK(edit_it->second < cur_pos);
          auto previous_update_ptr = static_cast<td_api::updateNotification *>(updates[edit_it->second - 1].get());
          CHECK(previous_update_ptr->notification_->id_ == notification_id);
          previous_update_ptr->notification_->type_ = std::move(update_ptr->notification_->type_);
          is_changed = true;
          update = nullptr;
          continue;
        }
        auto add_it = first_add_notification_pos.find(notification_id);
        if (add_it != first_add_notification_pos.end()) {
          VLOG(notifications) << "Move edit of " << notification_id << " in update " << cur_pos << " to update "
                              << add_it->second;
          CHECK(add_it->second < cur_pos);
          auto previous_update_ptr = static_cast<td_api::updateNotificationGroup *>(updates[add_it->second - 1].get());
          bool is_found = false;
          for (auto &notification : previous_update_ptr->added_notifications_) {
            if (notification->id_ == notification_id) {
              notification->type_ = std::move(update_ptr->notification_->type_);
              is_found = true;
              break;
            }
          }
          CHECK(is_found);
          is_changed = true;
          update = nullptr;
          continue;
        }

        // it is a first addition/edit of needed notification
        first_edit_notification_pos[notification_id] = cur_pos;
      }
    }
    if (!moved_deleted_notification_ids.empty()) {
      CHECK(first_notification_group_pos != 0);
      auto &update = updates[first_notification_group_pos - 1];
      CHECK(update->get_id() == td_api::updateNotificationGroup::ID);
      auto update_ptr = static_cast<td_api::updateNotificationGroup *>(update.get());
      append(update_ptr->removed_notification_ids_, std::move(moved_deleted_notification_ids));
      auto old_size = update_ptr->removed_notification_ids_.size();
      std::sort(update_ptr->removed_notification_ids_.begin(), update_ptr->removed_notification_ids_.end());
      update_ptr->removed_notification_ids_.erase(
          std::unique(update_ptr->removed_notification_ids_.begin(), update_ptr->removed_notification_ids_.end()),
          update_ptr->removed_notification_ids_.end());
      CHECK(old_size == update_ptr->removed_notification_ids_.size());
    }

    updates.erase(std::remove_if(updates.begin(), updates.end(), [](auto &update) { return update == nullptr; }),
                  updates.end());
    if (updates.empty()) {
      VLOG(notifications) << "There are no updates to send in " << NotificationGroupId(group_id);
      return;
    }

    size_t last_update_pos = 0;
    for (size_t i = 1; i < updates.size(); i++) {
      if (updates[last_update_pos]->get_id() == td_api::updateNotificationGroup::ID &&
          updates[i]->get_id() == td_api::updateNotificationGroup::ID) {
        auto last_update_ptr = static_cast<td_api::updateNotificationGroup *>(updates[last_update_pos].get());
        auto update_ptr = static_cast<td_api::updateNotificationGroup *>(updates[i].get());
        if (last_update_ptr->notification_settings_chat_id_ == update_ptr->notification_settings_chat_id_ &&
            last_update_ptr->is_silent_ == update_ptr->is_silent_) {
          if ((last_update_ptr->added_notifications_.empty() && update_ptr->added_notifications_.empty()) ||
              (last_update_ptr->removed_notification_ids_.empty() && update_ptr->removed_notification_ids_.empty())) {
            // combine updates
            VLOG(notifications) << "Combine " << as_notification_update(last_update_ptr) << " and "
                                << as_notification_update(update_ptr);
            CHECK(last_update_ptr->notification_group_id_ == update_ptr->notification_group_id_);
            CHECK(last_update_ptr->chat_id_ == update_ptr->chat_id_);
            last_update_ptr->total_count_ = update_ptr->total_count_;
            append(last_update_ptr->added_notifications_, std::move(update_ptr->added_notifications_));
            append(last_update_ptr->removed_notification_ids_, std::move(update_ptr->removed_notification_ids_));
            updates[i] = nullptr;
            is_changed = true;
            continue;
          }
        }
      }
      last_update_pos++;
      if (last_update_pos != i) {
        updates[last_update_pos] = std::move(updates[i]);
      }
    }
    updates.resize(last_update_pos + 1);
  }

  for (auto &update : updates) {
    VLOG(notifications) << "Send " << as_notification_update(update.get());
    send_closure(G()->td(), &Td::send_update, std::move(update));
  }
}

void NotificationManager::flush_all_pending_updates(bool include_delayed_chats, const char *source) {
  vector<NotificationGroupKey> ready_group_keys;
  for (auto &it : pending_updates_) {
    if (include_delayed_chats || running_get_chat_difference_.count(it.first) == 0) {
      auto group_it = get_group(NotificationGroupId(it.first));
      CHECK(group_it != groups_.end());
      ready_group_keys.push_back(group_it->first);
    }
  }

  // flush groups in reverse order to not exceed max_notification_group_count_
  std::sort(ready_group_keys.begin(), ready_group_keys.end());
  for (auto group_key : reversed(ready_group_keys)) {
    flush_pending_updates_timeout_.cancel_timeout(group_key.group_id.get());
    flush_pending_updates(group_key.group_id.get(), "after_get_difference");
  }
  if (include_delayed_chats) {
    CHECK(pending_updates_.empty());
  }
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
    Notification notification(pending_notification.notification_id, pending_notification.date,
                              std::move(pending_notification.type));
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
    add_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
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
    add_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
        group_key.group_id.get(), group_key.dialog_id.get(), group_key.dialog_id.get(), true, group.total_count,
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
    add_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
        group_key.group_id.get(), group_key.dialog_id.get(), 0, true, group.total_count, std::move(added_notifications),
        vector<int32>()));
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
      keep_notification_group_size_ +
          EXTRA_GROUP_SIZE) {  // ensure that we delete a lot of notifications simultaneously
    // keep only keep_notification_group_size_ last notifications in memory
    group.notifications.erase(
        group.notifications.begin(),
        group.notifications.begin() + (group.notifications.size() - keep_notification_group_size_));
  }

  groups_.emplace(std::move(final_group_key), std::move(group));
}

void NotificationManager::flush_all_pending_notifications() {
  std::multimap<int32, NotificationGroupId> group_ids;
  for (auto &group_it : groups_) {
    if (!group_it.second.pending_notifications.empty()) {
      group_ids.emplace(group_it.second.pending_notifications.back().date, group_it.first.group_id);
    }
  }

  // flush groups in order of last notification date
  for (auto &it : group_ids) {
    flush_pending_notifications_timeout_.cancel_timeout(it.second.get());
    flush_pending_notifications(it.second);
  }
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
  if (group_it == groups_.end()) {
    return;
  }
  auto &group = group_it->second;
  for (size_t i = 0; i < group.notifications.size(); i++) {
    auto &notification = group.notifications[i];
    if (notification.notification_id == notification_id) {
      notification.type = std::move(type);
      if (i + max_notification_group_size_ >= group.notifications.size()) {
        add_update_notification(group_it->first.group_id, group_it->first.dialog_id, notification);
      }
      return;
    }
  }
  for (auto &notification : group.pending_notifications) {
    if (notification.notification_id == notification_id) {
      notification.type = std::move(type);
      return;
    }
  }
}

void NotificationManager::on_notifications_removed(
    NotificationGroups::iterator &&group_it, vector<td_api::object_ptr<td_api::notification>> &&added_notifications,
    vector<int32> &&removed_notification_ids) {
  VLOG(notifications) << "In on_notifications_removed for " << group_it->first.group_id << " with "
                      << added_notifications.size() << " added notifications and " << removed_notification_ids.size()
                      << " removed notifications";
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
      add_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
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
    TODO load_notification_groups_from_database();
  }
  */
}

void NotificationManager::remove_added_notifications_from_pending_updates(
    NotificationGroupId group_id,
    std::function<bool(const td_api::object_ptr<td_api::notification> &notification)> is_removed) {
  auto it = pending_updates_.find(group_id.get());
  if (it == pending_updates_.end()) {
    return;
  }

  std::unordered_set<int32> removed_notification_ids;
  for (auto &update : it->second) {
    if (update == nullptr) {
      continue;
    }
    if (update->get_id() == td_api::updateNotificationGroup::ID) {
      auto update_ptr = static_cast<td_api::updateNotificationGroup *>(update.get());
      if (!removed_notification_ids.empty() && !update_ptr->removed_notification_ids_.empty()) {
        update_ptr->removed_notification_ids_.erase(
            std::remove_if(update_ptr->removed_notification_ids_.begin(), update_ptr->removed_notification_ids_.end(),
                           [&removed_notification_ids](auto &notification_id) {
                             return removed_notification_ids.count(notification_id) == 1;
                           }),
            update_ptr->removed_notification_ids_.end());
      }
      for (auto &notification : update_ptr->added_notifications_) {
        if (is_removed(notification)) {
          removed_notification_ids.insert(notification->id_);
          VLOG(notifications) << "Remove " << NotificationId(notification->id_) << " in " << group_id;
          notification = nullptr;
        }
      }
      update_ptr->added_notifications_.erase(
          std::remove_if(update_ptr->added_notifications_.begin(), update_ptr->added_notifications_.end(),
                         [](auto &notification) { return notification == nullptr; }),
          update_ptr->added_notifications_.end());
    } else {
      CHECK(update->get_id() == td_api::updateNotification::ID);
      auto update_ptr = static_cast<td_api::updateNotification *>(update.get());
      if (is_removed(update_ptr->notification_)) {
        removed_notification_ids.insert(update_ptr->notification_->id_);
        VLOG(notifications) << "Remove " << NotificationId(update_ptr->notification_->id_) << " in " << group_id;
        update = nullptr;
      }
    }
  }
}

void NotificationManager::remove_notification(NotificationGroupId group_id, NotificationId notification_id,
                                              bool is_permanent, Promise<Unit> &&promise) {
  if (!group_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification group identifier is invalid"));
  }
  if (!notification_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }

  if (is_disabled()) {
    return promise.set_value(Unit());
  }

  VLOG(notifications) << "Remove " << notification_id << " from " << group_id;

  // TODO remove notification from database by notification_id

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    return promise.set_value(Unit());
  }

  for (auto it = group_it->second.pending_notifications.begin(); it != group_it->second.pending_notifications.end();
       ++it) {
    if (it->notification_id == notification_id) {
      // notification is still pending, just delete it
      group_it->second.pending_notifications.erase(it);
      if (group_it->second.pending_notifications.empty()) {
        group_it->second.pending_notifications_flush_time = 0;
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

  remove_added_notifications_from_pending_updates(
      group_id, [notification_id](const td_api::object_ptr<td_api::notification> &notification) {
        return notification->id_ == notification_id.get();
      });

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

  VLOG(notifications) << "Remove " << group_id << " up to " << max_notification_id << " or " << max_message_id
                      << " with new_total_count = " << new_total_count;

  if (max_notification_id.is_valid()) {
    // TODO remove notifications from database by max_notification_id, save that they are removed
  }

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    VLOG(notifications) << "Can't find " << group_id;
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
    group_it->second.pending_notifications_flush_time = 0;
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
  } else {
    VLOG(notifications) << "Have new_total_count = " << new_total_count << " and " << removed_notification_ids.size()
                        << " removed notifications";
  }

  if (max_notification_id.is_valid()) {
    remove_added_notifications_from_pending_updates(
        group_id, [max_notification_id](const td_api::object_ptr<td_api::notification> &notification) {
          return notification->id_ <= max_notification_id.get();
        });
  } else {
    remove_added_notifications_from_pending_updates(
        group_id, [max_message_id](const td_api::object_ptr<td_api::notification> &notification) {
          return notification->type_->get_id() == td_api::notificationTypeNewMessage::ID &&
                 static_cast<const td_api::notificationTypeNewMessage *>(notification->type_.get())->message_->id_ <=
                     max_message_id.get();
        });
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

  auto new_max_notification_group_count_size_t = static_cast<size_t>(new_max_notification_group_count);
  if (new_max_notification_group_count_size_t == max_notification_group_count_) {
    return;
  }

  VLOG(notifications) << "Change max notification group count from " << max_notification_group_count_ << " to "
                      << new_max_notification_group_count;

  bool is_increased = new_max_notification_group_count_size_t > max_notification_group_count_;
  if (max_notification_group_count_ != 0) {
    flush_all_pending_notifications();
    flush_all_pending_updates(true, "on_notification_group_size_max_changed begin");

    size_t cur_pos = 0;
    size_t min_group_count = min(new_max_notification_group_count_size_t, max_notification_group_count_);
    size_t max_group_count = max(new_max_notification_group_count_size_t, max_notification_group_count_);
    for (auto it = groups_.begin(); it != groups_.end() && cur_pos < max_group_count; ++it, cur_pos++) {
      if (cur_pos < min_group_count) {
        continue;
      }

      auto &group_key = it->first;
      auto &group = it->second;
      CHECK(group.pending_notifications.empty());
      CHECK(pending_updates_.count(group_key.group_id.get()) == 0);

      if (is_increased) {
        send_add_group_update(group_key, group);
      } else {
        send_remove_group_update(group_key, group, vector<int32>());
      }
    }

    flush_all_pending_updates(true, "on_notification_group_size_max_changed end");
  }

  max_notification_group_count_ = new_max_notification_group_count_size_t;
  /*
  if (is_increased && last_loaded_group_key_ < get_last_updated_group_key()) {
    TODO load_notification_groups_from_database();
  }
  */
}

void NotificationManager::on_notification_group_size_max_changed() {
  if (is_disabled()) {
    return;
  }

  auto new_max_notification_group_size =
      G()->shared_config().get_option_integer("notification_group_size_max", DEFAULT_GROUP_SIZE_MAX);
  CHECK(MIN_NOTIFICATION_GROUP_SIZE_MAX <= new_max_notification_group_size &&
        new_max_notification_group_size <= MAX_NOTIFICATION_GROUP_SIZE_MAX);

  auto new_max_notification_group_size_size_t = static_cast<size_t>(new_max_notification_group_size);
  if (new_max_notification_group_size_size_t == max_notification_group_size_) {
    return;
  }

  VLOG(notifications) << "Change max notification group size from " << max_notification_group_size_ << " to "
                      << new_max_notification_group_size;

  if (max_notification_group_size_ != 0) {
    flush_all_pending_notifications();
    flush_all_pending_updates(true, "on_notification_group_size_max_changed");

    int32 left = max_notification_group_count_;
    for (auto it = groups_.begin(); it != groups_.end() && left > 0; ++it, left--) {
      auto &group_key = it->first;
      auto &group = it->second;
      CHECK(group.pending_notifications.empty());
      CHECK(pending_updates_.count(group_key.group_id.get()) == 0);

      vector<td_api::object_ptr<td_api::notification>> added_notifications;
      vector<int32> removed_notification_ids;
      auto notification_count = group.notifications.size();
      if (new_max_notification_group_size_size_t < max_notification_group_size_) {
        if (notification_count <= new_max_notification_group_size_size_t) {
          VLOG(notifications) << "There is no need to update " << group_key.group_id;
          continue;
        }
        for (size_t i = notification_count - min(notification_count, max_notification_group_size_);
             i < notification_count - new_max_notification_group_size_size_t; i++) {
          removed_notification_ids.push_back(group.notifications[i].notification_id.get());
        }
        CHECK(!removed_notification_ids.empty());
      } else {
        if (notification_count <= max_notification_group_size_) {
          VLOG(notifications) << "There is no need to update " << group_key.group_id;
          continue;
        }
        for (size_t i = notification_count - min(notification_count, new_max_notification_group_size_size_t);
             i < notification_count - max_notification_group_size_; i++) {
          added_notifications.push_back(get_notification_object(group_key.dialog_id, group.notifications[i]));
          if (added_notifications.back()->type_ == nullptr) {
            added_notifications.pop_back();
          }
        }
        if (new_max_notification_group_size_size_t > notification_count &&
            static_cast<size_t>(group.total_count) > notification_count) {
          // TODO load more notifications in the group from the message database
        }
        if (added_notifications.empty()) {
          continue;
        }
      }
      auto update = td_api::make_object<td_api::updateNotificationGroup>(
          group_key.group_id.get(), group_key.dialog_id.get(), group_key.dialog_id.get(), true, group.total_count,
          std::move(added_notifications), std::move(removed_notification_ids));
      VLOG(notifications) << "Send " << as_notification_update(update.get());
      send_closure(G()->td(), &Td::send_update, std::move(update));
    }
  }

  max_notification_group_size_ = new_max_notification_group_size_size_t;
  keep_notification_group_size_ =
      max_notification_group_size_ + clamp(max_notification_group_size_, EXTRA_GROUP_SIZE / 2, EXTRA_GROUP_SIZE);
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

void NotificationManager::before_get_difference() {
  running_get_difference_ = true;
}

void NotificationManager::after_get_difference() {
  CHECK(running_get_difference_);
  running_get_difference_ = false;
  flush_pending_notifications_timeout_.set_timeout_in(0, MIN_NOTIFICATION_DELAY_MS * 1e-3);
}

void NotificationManager::after_get_difference_impl() {
  if (running_get_difference_) {
    return;
  }

  VLOG(notifications) << "After get difference";
  flush_all_pending_updates(false, "after_get_difference");
}

void NotificationManager::before_get_chat_difference(NotificationGroupId group_id) {
  VLOG(notifications) << "Before get chat difference in " << group_id;
  CHECK(group_id.is_valid());
  running_get_chat_difference_.insert(group_id.get());
}

void NotificationManager::after_get_chat_difference(NotificationGroupId group_id) {
  VLOG(notifications) << "After get chat difference in " << group_id;
  CHECK(group_id.is_valid());
  auto erased_count = running_get_chat_difference_.erase(group_id.get());
  if (erased_count == 1) {
    flush_pending_notifications_timeout_.set_timeout_in(-group_id.get(), MIN_NOTIFICATION_DELAY_MS * 1e-3);
  }
}

void NotificationManager::after_get_chat_difference_impl(NotificationGroupId group_id) {
  if (running_get_chat_difference_.count(group_id.get()) == 1) {
    return;
  }

  VLOG(notifications) << "After get chat difference in " << group_id;
  CHECK(group_id.is_valid());
  if (!running_get_difference_ && pending_updates_.count(group_id.get()) == 1) {
    flush_pending_updates_timeout_.cancel_timeout(group_id.get());
    flush_pending_updates(group_id.get(), "after_get_chat_difference");
  }
}

}  // namespace td
