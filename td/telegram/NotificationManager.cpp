//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DeviceTokenManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/SecretChatId.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"

#include "td/mtproto/AuthKey.h"
#include "td/mtproto/PacketInfo.h"
#include "td/mtproto/Transport.h"

#include "td/utils/as.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace td {

int VERBOSITY_NAME(notifications) = VERBOSITY_NAME(WARNING);

class SetContactSignUpNotificationQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetContactSignUpNotificationQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(bool is_disabled) {
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::account_setContactSignUpNotification(is_disabled))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_setContactSignUpNotification>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->close_flag()) {
      LOG(ERROR) << "Receive error for set contact sign up notification: " << status;
    }
    promise_.set_error(std::move(status));
  }
};

class GetContactSignUpNotificationQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit GetContactSignUpNotificationQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(create_storer(telegram_api::account_getContactSignUpNotification())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::account_getContactSignUpNotification>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->notification_manager_->on_get_disable_contact_registered_notifications(result_ptr.ok());
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->close_flag() || 1) {
      LOG(ERROR) << "Receive error for get contact sign up notification: " << status;
    }
    promise_.set_error(std::move(status));
  }
};

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
  VLOG(notifications) << "Ready to flush pending notifications for notification group " << group_id_int;
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
  return !td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot() || G()->close_flag();
}

namespace {

struct ActiveNotificationsUpdate {
  const td_api::updateActiveNotifications *update;
};

StringBuilder &operator<<(StringBuilder &string_builder, const ActiveNotificationsUpdate &update) {
  if (update.update == nullptr) {
    return string_builder << "null";
  }
  string_builder << "update[\n";
  for (auto &group : update.update->groups_) {
    vector<int32> added_notification_ids;
    for (auto &notification : group->notifications_) {
      added_notification_ids.push_back(notification->id_);
    }

    string_builder << "    [" << NotificationGroupId(group->id_) << " of type "
                   << get_notification_group_type(group->type_) << " from " << DialogId(group->chat_id_)
                   << "; total_count = " << group->total_count_ << ", restore " << added_notification_ids << "]\n";
  }
  return string_builder << ']';
}

ActiveNotificationsUpdate as_active_notifications_update(const td_api::updateActiveNotifications *update) {
  return ActiveNotificationsUpdate{update};
}

}  // namespace

string NotificationManager::get_is_contact_registered_notifications_synchronized_key() {
  return "notifications_contact_registered_sync_state";
}

void NotificationManager::start_up() {
  init();
}

void NotificationManager::init() {
  if (is_disabled()) {
    return;
  }

  disable_contact_registered_notifications_ =
      G()->shared_config().get_option_boolean("disable_contact_registered_notifications");
  auto sync_state = G()->td_db()->get_binlog_pmc()->get(get_is_contact_registered_notifications_synchronized_key());
  if (sync_state.empty()) {
    sync_state = "00";
  }
  contact_registered_notifications_sync_state_ = static_cast<SyncState>(sync_state[0] - '0');
  VLOG(notifications) << "Loaded disable_contact_registered_notifications = "
                      << disable_contact_registered_notifications_ << " in state " << sync_state;
  if (contact_registered_notifications_sync_state_ != SyncState::Completed ||
      static_cast<bool>(sync_state[1] - '0') != disable_contact_registered_notifications_) {
    run_contact_registered_notifications_sync();
  }

  current_notification_id_ =
      NotificationId(to_integer<int32>(G()->td_db()->get_binlog_pmc()->get("notification_id_current")));
  current_notification_group_id_ =
      NotificationGroupId(to_integer<int32>(G()->td_db()->get_binlog_pmc()->get("notification_group_id_current")));

  on_notification_group_count_max_changed(false);
  on_notification_group_size_max_changed();

  on_online_cloud_timeout_changed();
  on_notification_cloud_delay_changed();
  on_notification_default_delay_changed();

  last_loaded_notification_group_key_.last_notification_date = std::numeric_limits<int32>::max();
  if (max_notification_group_count_ != 0) {
    int32 loaded_groups = 0;
    int32 needed_groups = static_cast<int32>(max_notification_group_count_);
    do {
      loaded_groups += load_message_notification_groups_from_database(needed_groups, false);
    } while (loaded_groups < needed_groups && last_loaded_notification_group_key_.last_notification_date != 0);

    auto update = get_update_active_notifications();
    VLOG(notifications) << "Send " << as_active_notifications_update(update.get());
    send_closure(G()->td(), &Td::send_update, std::move(update));
  }

  auto call_notification_group_ids_string = G()->td_db()->get_binlog_pmc()->get("notification_call_group_ids");
  if (!call_notification_group_ids_string.empty()) {
    call_notification_group_ids_ = transform(full_split(call_notification_group_ids_string, ','), [](Slice str) {
      return NotificationGroupId{to_integer_safe<int32>(str).ok()};
    });
    VLOG(notifications) << "Load call_notification_group_ids_ = " << call_notification_group_ids_;
    for (auto &group_id : call_notification_group_ids_) {
      if (group_id.get() > current_notification_group_id_.get()) {
        LOG(ERROR) << "Fix current notification group id from " << current_notification_group_id_ << " to " << group_id;
        current_notification_group_id_ = group_id;
        G()->td_db()->get_binlog_pmc()->set("notification_group_id_current",
                                            to_string(current_notification_group_id_.get()));
      }
      available_call_notification_group_ids_.insert(group_id);
    }
  }

  auto notification_announcement_ids_string = G()->td_db()->get_binlog_pmc()->get("notification_announcement_ids");
  if (!notification_announcement_ids_string.empty()) {
    VLOG(notifications) << "Load announcement ids = " << notification_announcement_ids_string;
    auto ids = transform(full_split(notification_announcement_ids_string, ','),
                         [](Slice str) { return to_integer_safe<int32>(str).ok(); });
    CHECK(ids.size() % 2 == 0);
    bool is_changed = false;
    auto min_date = G()->unix_time() - ANNOUNCEMENT_ID_CACHE_TIME;
    for (size_t i = 0; i < ids.size(); i += 2) {
      auto id = ids[i];
      auto date = ids[i + 1];
      if (date < min_date) {
        is_changed = true;
        continue;
      }
      announcement_id_date_.emplace(id, date);
    }
    if (is_changed) {
      save_announcement_ids();
    }
  }

  class StateCallback : public StateManager::Callback {
   public:
    explicit StateCallback(ActorId<NotificationManager> parent) : parent_(std::move(parent)) {
    }
    bool on_online(bool is_online) override {
      if (is_online) {
        send_closure(parent_, &NotificationManager::flush_all_pending_notifications);
      }
      return parent_.is_alive();
    }

   private:
    ActorId<NotificationManager> parent_;
  };
  send_closure(G()->state_manager(), &StateManager::add_callback, make_unique<StateCallback>(actor_id(this)));
}

void NotificationManager::save_announcement_ids() {
  auto min_date = G()->unix_time() - ANNOUNCEMENT_ID_CACHE_TIME;
  vector<int32> ids;
  for (auto &it : announcement_id_date_) {
    auto id = it.first;
    auto date = it.second;
    if (date < min_date) {
      continue;
    }
    ids.push_back(id);
    ids.push_back(date);
  }

  VLOG(notifications) << "Save announcement ids " << ids;
  if (ids.empty()) {
    G()->td_db()->get_binlog_pmc()->erase("notification_announcement_ids");
    return;
  }

  auto notification_announcement_ids_string = implode(transform(ids, [](int32 id) { return to_string(id); }), ',');
  G()->td_db()->get_binlog_pmc()->set("notification_announcement_ids", notification_announcement_ids_string);
}

td_api::object_ptr<td_api::updateActiveNotifications> NotificationManager::get_update_active_notifications() const {
  auto needed_groups = max_notification_group_count_;
  vector<td_api::object_ptr<td_api::notificationGroup>> groups;
  for (auto &group : groups_) {
    if (needed_groups == 0 || group.first.last_notification_date == 0) {
      break;
    }
    needed_groups--;

    vector<td_api::object_ptr<td_api::notification>> notifications;
    for (auto &notification : group.second.notifications) {
      auto notification_object = get_notification_object(group.first.dialog_id, notification);
      if (notification_object->type_ != nullptr) {
        notifications.push_back(std::move(notification_object));
      }
    }
    if (!notifications.empty()) {
      groups.push_back(td_api::make_object<td_api::notificationGroup>(
          group.first.group_id.get(), get_notification_group_type_object(group.second.type),
          group.first.dialog_id.get(), group.second.total_count, std::move(notifications)));
    }
  }

  return td_api::make_object<td_api::updateActiveNotifications>(std::move(groups));
}

void NotificationManager::tear_down() {
  parent_.reset();
}

NotificationManager::NotificationGroups::iterator NotificationManager::add_group(NotificationGroupKey &&group_key,
                                                                                 NotificationGroup &&group) {
  bool is_inserted = group_keys_.emplace(group_key.group_id, group_key).second;
  CHECK(is_inserted);
  return groups_.emplace(std::move(group_key), std::move(group)).first;
}

NotificationManager::NotificationGroups::iterator NotificationManager::get_group(NotificationGroupId group_id) {
  auto group_keys_it = group_keys_.find(group_id);
  if (group_keys_it != group_keys_.end()) {
    return groups_.find(group_keys_it->second);
  }
  return groups_.end();
}

void NotificationManager::load_group_force(NotificationGroupId group_id) {
  if (is_disabled() || max_notification_group_count_ == 0) {
    return;
  }

  auto group_it = get_group_force(group_id, true);
  CHECK(group_it != groups_.end());
}

NotificationManager::NotificationGroups::iterator NotificationManager::get_group_force(NotificationGroupId group_id,
                                                                                       bool send_update) {
  auto group_it = get_group(group_id);
  if (group_it != groups_.end()) {
    return group_it;
  }

  if (std::find(call_notification_group_ids_.begin(), call_notification_group_ids_.end(), group_id) !=
      call_notification_group_ids_.end()) {
    return groups_.end();
  }

  auto message_group = td_->messages_manager_->get_message_notification_group_force(group_id);
  if (!message_group.dialog_id.is_valid()) {
    return groups_.end();
  }

  NotificationGroupKey group_key(group_id, message_group.dialog_id, 0);
  for (auto &notification : message_group.notifications) {
    if (notification.date > group_key.last_notification_date) {
      group_key.last_notification_date = notification.date;
    }
    if (notification.notification_id.get() > current_notification_id_.get()) {
      LOG(ERROR) << "Fix current notification id from " << current_notification_id_ << " to "
                 << notification.notification_id;
      current_notification_id_ = notification.notification_id;
      G()->td_db()->get_binlog_pmc()->set("notification_id_current", to_string(current_notification_id_.get()));
    }
  }
  if (group_id.get() > current_notification_group_id_.get()) {
    LOG(ERROR) << "Fix current notification group id from " << current_notification_group_id_ << " to " << group_id;
    current_notification_group_id_ = group_id;
    G()->td_db()->get_binlog_pmc()->set("notification_group_id_current",
                                        to_string(current_notification_group_id_.get()));
  }

  NotificationGroup group;
  group.type = message_group.type;
  group.total_count = message_group.total_count;
  group.notifications = std::move(message_group.notifications);

  VLOG(notifications) << "Finish to load " << group_id << " of type " << message_group.type << " with total_count "
                      << message_group.total_count << " and notifications " << group.notifications;

  if (send_update && group_key.last_notification_date != 0) {
    auto last_group_key = get_last_updated_group_key();
    if (group_key < last_group_key) {
      if (last_group_key.last_notification_date != 0) {
        send_remove_group_update(last_group_key, groups_[last_group_key], vector<int32>());
      }
      send_add_group_update(group_key, group);
    }
  }
  return add_group(std::move(group_key), std::move(group));
}

void NotificationManager::delete_group(NotificationGroups::iterator &&group_it) {
  bool is_erased = group_keys_.erase(group_it->first.group_id);
  CHECK(is_erased);
  groups_.erase(group_it);
}

int32 NotificationManager::load_message_notification_groups_from_database(int32 limit, bool send_update) {
  CHECK(limit > 0);
  if (last_loaded_notification_group_key_.last_notification_date == 0) {
    // everything was already loaded
    return 0;
  }

  vector<NotificationGroupKey> group_keys = td_->messages_manager_->get_message_notification_group_keys_from_database(
      last_loaded_notification_group_key_, limit);
  last_loaded_notification_group_key_ =
      group_keys.size() == static_cast<size_t>(limit) ? group_keys.back() : NotificationGroupKey();

  int32 result = 0;
  for (auto &group_key : group_keys) {
    auto group_it = get_group_force(group_key.group_id, send_update);
    LOG_CHECK(group_it != groups_.end()) << call_notification_group_ids_ << " " << group_key.group_id << " "
                                         << current_notification_group_id_;
    CHECK(group_it->first.dialog_id.is_valid());
    if (!(last_loaded_notification_group_key_ < group_it->first)) {
      result++;
    }
  }
  return result;
}

NotificationId NotificationManager::get_first_notification_id(const NotificationGroup &group) {
  if (!group.notifications.empty()) {
    return group.notifications[0].notification_id;
  }
  if (!group.pending_notifications.empty()) {
    return group.pending_notifications[0].notification_id;
  }
  return NotificationId();
}

MessageId NotificationManager::get_first_message_id(const NotificationGroup &group) {
  // it's fine to return MessageId() if first notification has no message_id, because
  // non-message notification can't be mixed with message notifications
  if (!group.notifications.empty()) {
    return group.notifications[0].type->get_message_id();
  }
  if (!group.pending_notifications.empty()) {
    return group.pending_notifications[0].type->get_message_id();
  }
  return MessageId();
}

void NotificationManager::load_message_notifications_from_database(const NotificationGroupKey &group_key,
                                                                   NotificationGroup &group, size_t desired_size) {
  if (!G()->parameters().use_message_db) {
    return;
  }
  if (group.is_loaded_from_database || group.is_being_loaded_from_database ||
      group.type == NotificationGroupType::Calls) {
    return;
  }
  if (group.total_count == 0) {
    return;
  }

  VLOG(notifications) << "Trying to load up to " << desired_size << " notifications in " << group_key.group_id
                      << " with " << group.notifications.size() << " current notifications";

  group.is_being_loaded_from_database = true;

  CHECK(desired_size > group.notifications.size());
  size_t limit = desired_size - group.notifications.size();
  auto first_notification_id = get_first_notification_id(group);
  auto from_notification_id = first_notification_id.is_valid() ? first_notification_id : NotificationId::max();
  auto first_message_id = get_first_message_id(group);
  auto from_message_id = first_message_id.is_valid() ? first_message_id : MessageId::max();
  send_closure(G()->messages_manager(), &MessagesManager::get_message_notifications_from_database, group_key.dialog_id,
               group_key.group_id, from_notification_id, from_message_id, static_cast<int32>(limit),
               PromiseCreator::lambda([actor_id = actor_id(this), group_id = group_key.group_id,
                                       limit](Result<vector<Notification>> r_notifications) {
                 send_closure_later(actor_id, &NotificationManager::on_get_message_notifications_from_database,
                                    group_id, limit, std::move(r_notifications));
               }));
}

void NotificationManager::on_get_message_notifications_from_database(NotificationGroupId group_id, size_t limit,
                                                                     Result<vector<Notification>> r_notifications) {
  auto group_it = get_group(group_id);
  CHECK(group_it != groups_.end());
  auto &group = group_it->second;
  CHECK(group.is_being_loaded_from_database == true);
  group.is_being_loaded_from_database = false;

  if (r_notifications.is_error()) {
    group.is_loaded_from_database = true;  // do not try again to load it
    return;
  }
  auto notifications = r_notifications.move_as_ok();

  CHECK(limit > 0);
  if (notifications.empty()) {
    group.is_loaded_from_database = true;
  }

  auto first_notification_id = get_first_notification_id(group);
  if (first_notification_id.is_valid()) {
    while (!notifications.empty() && notifications.back().notification_id.get() >= first_notification_id.get()) {
      // possible if notifications was added after the database request was sent
      notifications.pop_back();
    }
  }

  add_notifications_to_group_begin(std::move(group_it), std::move(notifications));

  group_it = get_group(group_id);
  CHECK(group_it != groups_.end());
  if (max_notification_group_size_ > group_it->second.notifications.size()) {
    load_message_notifications_from_database(group_it->first, group_it->second, keep_notification_group_size_);
  }
}

void NotificationManager::add_notifications_to_group_begin(NotificationGroups::iterator group_it,
                                                           vector<Notification> notifications) {
  CHECK(group_it != groups_.end());

  if (notifications.empty()) {
    return;
  }
  VLOG(notifications) << "Add to beginning of " << group_it->first << " of size "
                      << group_it->second.notifications.size() << ' ' << notifications;

  auto group_key = group_it->first;
  auto final_group_key = group_key;
  for (auto &notification : notifications) {
    if (notification.date > final_group_key.last_notification_date) {
      final_group_key.last_notification_date = notification.date;
    }
  }
  CHECK(final_group_key.last_notification_date != 0);

  bool is_position_changed = final_group_key.last_notification_date != group_key.last_notification_date;

  NotificationGroup group = std::move(group_it->second);
  if (is_position_changed) {
    VLOG(notifications) << "Position of notification group is changed from " << group_key << " to " << final_group_key;
    delete_group(std::move(group_it));
  }

  auto last_group_key = get_last_updated_group_key();
  bool was_updated = false;
  bool is_updated = false;
  if (is_position_changed) {
    was_updated = group_key.last_notification_date != 0 && group_key < last_group_key;
    is_updated = final_group_key.last_notification_date != 0 && final_group_key < last_group_key;
  } else {
    CHECK(group_key.last_notification_date != 0);
    was_updated = is_updated = !(last_group_key < group_key);
  }

  if (!is_updated) {
    CHECK(!was_updated);
    VLOG(notifications) << "There is no need to send updateNotificationGroup in " << group_key
                        << ", because of newer notification groups";
    group.notifications.insert(group.notifications.begin(), std::make_move_iterator(notifications.begin()),
                               std::make_move_iterator(notifications.end()));
  } else {
    if (!was_updated) {
      if (last_group_key.last_notification_date != 0) {
        // need to remove last notification group to not exceed max_notification_group_count_
        send_remove_group_update(last_group_key, groups_[last_group_key], vector<int32>());
      }
      send_add_group_update(group_key, group);
    }

    vector<Notification> new_notifications;
    vector<td_api::object_ptr<td_api::notification>> added_notifications;
    new_notifications.reserve(notifications.size());
    added_notifications.reserve(notifications.size());
    for (auto &notification : notifications) {
      added_notifications.push_back(get_notification_object(group_key.dialog_id, notification));
      if (added_notifications.back()->type_ == nullptr) {
        added_notifications.pop_back();
      } else {
        new_notifications.push_back(std::move(notification));
      }
    }
    notifications = std::move(new_notifications);

    size_t old_notification_count = group.notifications.size();
    auto updated_notification_count = old_notification_count < max_notification_group_size_
                                          ? max_notification_group_size_ - old_notification_count
                                          : 0;
    if (added_notifications.size() > updated_notification_count) {
      added_notifications.erase(added_notifications.begin(), added_notifications.end() - updated_notification_count);
    }
    auto new_notification_count = old_notification_count < keep_notification_group_size_
                                      ? keep_notification_group_size_ - old_notification_count
                                      : 0;
    if (new_notification_count > notifications.size()) {
      new_notification_count = notifications.size();
    }
    if (new_notification_count != 0) {
      VLOG(notifications) << "Add " << new_notification_count << " notifications to " << group_key.group_id
                          << " with current size " << group.notifications.size();
      group.notifications.insert(group.notifications.begin(),
                                 std::make_move_iterator(notifications.end() - new_notification_count),
                                 std::make_move_iterator(notifications.end()));
    }

    if (!added_notifications.empty()) {
      add_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
          group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(), 0, true,
          group.total_count, std::move(added_notifications), vector<int32>()));
    }
  }

  if (is_position_changed) {
    add_group(std::move(final_group_key), std::move(group));
  } else {
    group_it->second = std::move(group);
  }
}

size_t NotificationManager::get_max_notification_group_size() const {
  return max_notification_group_size_;
}

NotificationId NotificationManager::get_max_notification_id() const {
  return current_notification_id_;
}

NotificationId NotificationManager::get_next_notification_id() {
  if (is_disabled()) {
    return NotificationId();
  }
  if (current_notification_id_.get() == std::numeric_limits<int32>::max()) {
    LOG(ERROR) << "Notification id overflowed";
    return NotificationId();
  }

  current_notification_id_ = NotificationId(current_notification_id_.get() + 1);
  G()->td_db()->get_binlog_pmc()->set("notification_id_current", to_string(current_notification_id_.get()));
  return current_notification_id_;
}

NotificationGroupId NotificationManager::get_next_notification_group_id() {
  if (is_disabled()) {
    return NotificationGroupId();
  }
  if (current_notification_group_id_.get() == std::numeric_limits<int32>::max()) {
    LOG(ERROR) << "Notification group id overflowed";
    return NotificationGroupId();
  }

  current_notification_group_id_ = NotificationGroupId(current_notification_group_id_.get() + 1);
  G()->td_db()->get_binlog_pmc()->set("notification_group_id_current", to_string(current_notification_group_id_.get()));
  return current_notification_group_id_;
}

void NotificationManager::try_reuse_notification_group_id(NotificationGroupId group_id) {
  if (is_disabled()) {
    return;
  }
  if (!group_id.is_valid()) {
    return;
  }

  VLOG(notifications) << "Trying to reuse " << group_id;
  if (group_id != current_notification_group_id_) {
    // may be implemented in the future
    return;
  }

  auto group_it = get_group(group_id);
  if (group_it != groups_.end()) {
    CHECK(group_it->first.last_notification_date == 0);
    LOG_CHECK(group_it->second.total_count == 0)
        << running_get_difference_ << " " << pending_notification_update_count_ << " "
        << pending_updates_[group_id.get()].size() << " " << group_it->first << " " << group_it->second;
    CHECK(group_it->second.notifications.empty());
    CHECK(group_it->second.pending_notifications.empty());
    CHECK(!group_it->second.is_being_loaded_from_database);
    delete_group(std::move(group_it));

    CHECK(running_get_chat_difference_.count(group_id.get()) == 0);

    flush_pending_notifications_timeout_.cancel_timeout(group_id.get());
    flush_pending_updates_timeout_.cancel_timeout(group_id.get());
    if (pending_updates_.erase(group_id.get()) == 1) {
      on_pending_notification_update_count_changed(-1, group_id.get(), "try_reuse_notification_group_id");
    }
  }

  current_notification_group_id_ = NotificationGroupId(current_notification_group_id_.get() - 1);
  G()->td_db()->get_binlog_pmc()->set("notification_group_id_current", to_string(current_notification_group_id_.get()));
}

NotificationGroupKey NotificationManager::get_last_updated_group_key() const {
  size_t left = max_notification_group_count_;
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

int32 NotificationManager::get_notification_delay_ms(DialogId dialog_id, const PendingNotification &notification,
                                                     int32 min_delay_ms) const {
  if (dialog_id.get_type() == DialogType::SecretChat) {
    return MIN_NOTIFICATION_DELAY_MS;  // there is no reason to delay notifications in secret chats
  }
  if (!notification.type->can_be_delayed()) {
    return MIN_NOTIFICATION_DELAY_MS;
  }

  auto delay_ms = [&]() {
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
  return max(max(min_delay_ms, delay_ms) - passed_time_ms, MIN_NOTIFICATION_DELAY_MS);
}

void NotificationManager::add_notification(NotificationGroupId group_id, NotificationGroupType group_type,
                                           DialogId dialog_id, int32 date, DialogId notification_settings_dialog_id,
                                           bool is_silent, int32 min_delay_ms, NotificationId notification_id,
                                           unique_ptr<NotificationType> type) {
  if (is_disabled() || max_notification_group_count_ == 0) {
    return;
  }

  CHECK(group_id.is_valid());
  CHECK(dialog_id.is_valid());
  CHECK(notification_settings_dialog_id.is_valid());
  CHECK(notification_id.is_valid());
  CHECK(type != nullptr);
  VLOG(notifications) << "Add " << notification_id << " to " << group_id << " of type " << group_type << " in "
                      << dialog_id << " with settings from " << notification_settings_dialog_id
                      << (is_silent ? "   silently" : " with sound") << ": " << *type;

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    group_it = add_group(NotificationGroupKey(group_id, dialog_id, 0), NotificationGroup());
  }
  if (group_it->second.notifications.empty() && group_it->second.pending_notifications.empty()) {
    group_it->second.type = group_type;
  }
  CHECK(group_it->second.type == group_type);

  PendingNotification notification;
  notification.date = date;
  notification.settings_dialog_id = notification_settings_dialog_id;
  notification.is_silent = is_silent;
  notification.notification_id = notification_id;
  notification.type = std::move(type);

  auto delay_ms = get_notification_delay_ms(dialog_id, notification, min_delay_ms);
  VLOG(notifications) << "Delay " << notification_id << " for " << delay_ms << " milliseconds";
  auto flush_time = delay_ms * 0.001 + Time::now();

  NotificationGroup &group = group_it->second;
  if (group.pending_notifications_flush_time == 0 || flush_time < group.pending_notifications_flush_time) {
    group.pending_notifications_flush_time = flush_time;
    flush_pending_notifications_timeout_.set_timeout_at(group_id.get(), group.pending_notifications_flush_time);
  }
  if (group.pending_notifications.empty()) {
    on_pending_notification_update_count_changed(1, group_id.get(), "add_notification");
  }
  group.pending_notifications.push_back(std::move(notification));
}

namespace {

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

      return string_builder << "update[" << NotificationGroupId(p->notification_group_id_) << " of type "
                            << get_notification_group_type(p->type_) << " from " << DialogId(p->chat_id_)
                            << " with settings from " << DialogId(p->notification_settings_chat_id_)
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

}  // namespace

void NotificationManager::add_update(int32 group_id, td_api::object_ptr<td_api::Update> update) {
  VLOG(notifications) << "Add " << as_notification_update(update.get());
  auto &updates = pending_updates_[group_id];
  if (updates.empty()) {
    on_pending_notification_update_count_changed(1, group_id, "add_update");
  }
  updates.push_back(std::move(update));
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

  if (is_destroyed_) {
    return;
  }

  VLOG(notifications) << "Send " << updates.size() << " pending updates in " << NotificationGroupId(group_id)
                      << " from " << source;
  for (auto &update : updates) {
    VLOG(notifications) << "Have " << as_notification_update(update.get());
  }

  updates.erase(std::remove_if(updates.begin(), updates.end(), [](auto &update) { return update == nullptr; }),
                updates.end());

  // if a notification was added, then deleted and then re-added we need to keep
  // first addition, because it can be with sound,
  // deletion, because number of notification should never exceed max_notification_group_size_,
  // and second addition, because we has kept the deletion

  // calculate last state of all notifications
  std::unordered_set<int32> added_notification_ids;
  std::unordered_set<int32> edited_notification_ids;
  std::unordered_set<int32> removed_notification_ids;
  for (auto &update : updates) {
    CHECK(update != nullptr);
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
        if (!removed_notification_ids.insert(notification_id).second) {
          // sometimes there can be deletion of notification without previous addition, because the notification
          // has already been deleted at the time of addition and get_notification_object_type was nullptr
          VLOG(notifications) << "Remove duplicated deletion of " << notification_id;
          notification_id = 0;
        }
      }
      update_ptr->removed_notification_ids_.erase(
          std::remove_if(update_ptr->removed_notification_ids_.begin(), update_ptr->removed_notification_ids_.end(),
                         [](auto &notification_id) { return notification_id == 0; }),
          update_ptr->removed_notification_ids_.end());
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

  auto group_key = group_keys_[NotificationGroupId(group_id)];
  bool is_hidden = group_key.last_notification_date == 0 || get_last_updated_group_key() < group_key;
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

      CHECK(update != nullptr);
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
              previous_update_ptr->type_ = std::move(update_ptr->type_);
              previous_update_ptr->total_count_ = update_ptr->total_count_;
              is_changed = true;
              update = nullptr;
              break;
            }
          }
          if (update != nullptr && cur_pos == 1) {
            bool is_empty_group =
                added_notification_ids.empty() && edited_notification_ids.empty() && update_ptr->total_count_ == 0;
            if (updates.size() > 1 || (is_hidden && !is_empty_group)) {
              VLOG(notifications) << "Remove empty update " << cur_pos;
              CHECK(moved_deleted_notification_ids.empty());
              is_changed = true;
              update = nullptr;
            }
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
      break;
    }

    auto has_common_notifications = [](const vector<td_api::object_ptr<td_api::notification>> &notifications,
                                       const vector<int32> &notification_ids) {
      for (auto &notification : notifications) {
        if (std::find(notification_ids.begin(), notification_ids.end(), notification->id_) != notification_ids.end()) {
          return true;
        }
      }
      return false;
    };

    size_t last_update_pos = 0;
    for (size_t i = 1; i < updates.size(); i++) {
      if (updates[last_update_pos]->get_id() == td_api::updateNotificationGroup::ID &&
          updates[i]->get_id() == td_api::updateNotificationGroup::ID) {
        auto last_update_ptr = static_cast<td_api::updateNotificationGroup *>(updates[last_update_pos].get());
        auto update_ptr = static_cast<td_api::updateNotificationGroup *>(updates[i].get());
        if ((last_update_ptr->notification_settings_chat_id_ == update_ptr->notification_settings_chat_id_ ||
             last_update_ptr->added_notifications_.empty()) &&
            !has_common_notifications(last_update_ptr->added_notifications_, update_ptr->removed_notification_ids_) &&
            !has_common_notifications(update_ptr->added_notifications_, last_update_ptr->removed_notification_ids_)) {
          // combine updates
          VLOG(notifications) << "Combine " << as_notification_update(last_update_ptr) << " and "
                              << as_notification_update(update_ptr);
          CHECK(last_update_ptr->notification_group_id_ == update_ptr->notification_group_id_);
          CHECK(last_update_ptr->chat_id_ == update_ptr->chat_id_);
          if (last_update_ptr->is_silent_ && !update_ptr->is_silent_) {
            last_update_ptr->is_silent_ = false;
          }
          last_update_ptr->notification_settings_chat_id_ = update_ptr->notification_settings_chat_id_;
          last_update_ptr->type_ = std::move(update_ptr->type_);
          last_update_ptr->total_count_ = update_ptr->total_count_;
          append(last_update_ptr->added_notifications_, std::move(update_ptr->added_notifications_));
          append(last_update_ptr->removed_notification_ids_, std::move(update_ptr->removed_notification_ids_));
          updates[i] = nullptr;
          is_changed = true;
          continue;
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
    CHECK(update != nullptr);
    if (update->get_id() == td_api::updateNotificationGroup::ID) {
      auto update_ptr = static_cast<td_api::updateNotificationGroup *>(update.get());
      std::sort(update_ptr->added_notifications_.begin(), update_ptr->added_notifications_.end(),
                [](const auto &lhs, const auto &rhs) { return lhs->id_ < rhs->id_; });
      std::sort(update_ptr->removed_notification_ids_.begin(), update_ptr->removed_notification_ids_.end());
    }
    VLOG(notifications) << "Send " << as_notification_update(update.get());
    send_closure(G()->td(), &Td::send_update, std::move(update));
  }
  on_pending_notification_update_count_changed(-1, group_id, "flush_pending_updates");
}

void NotificationManager::flush_all_pending_updates(bool include_delayed_chats, const char *source) {
  VLOG(notifications) << "Flush all pending notification updates "
                      << (include_delayed_chats ? "with delayed chats " : "") << "from " << source;
  if (!include_delayed_chats && running_get_difference_) {
    return;
  }

  vector<NotificationGroupKey> ready_group_keys;
  for (const auto &it : pending_updates_) {
    if (include_delayed_chats || running_get_chat_difference_.count(it.first) == 0) {
      auto group_it = get_group(NotificationGroupId(it.first));
      CHECK(group_it != groups_.end());
      ready_group_keys.push_back(group_it->first);
    }
  }

  // flush groups in reverse order to not exceed max_notification_group_count_
  VLOG(notifications) << "Flush pending updates in " << ready_group_keys.size() << " notification groups";
  std::sort(ready_group_keys.begin(), ready_group_keys.end());
  for (auto group_key : reversed(ready_group_keys)) {
    flush_pending_updates_timeout_.cancel_timeout(group_key.group_id.get());
    flush_pending_updates(group_key.group_id.get(), "flush_all_pending_updates");
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

  VLOG(notifications) << "Do flush " << pending_notifications.size() << " pending notifications in " << group_key
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
    added_notifications.erase(added_notifications.begin(), added_notifications.end() - max_notification_group_size_);
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
        group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(),
        pending_notifications[0].settings_dialog_id.get(), pending_notifications[0].is_silent, group.total_count,
        std::move(added_notifications), std::move(removed_notification_ids)));
  } else {
    CHECK(removed_notification_ids.empty());
  }
  pending_notifications.clear();
}

td_api::object_ptr<td_api::updateNotificationGroup> NotificationManager::get_remove_group_update(
    const NotificationGroupKey &group_key, const NotificationGroup &group,
    vector<int32> &&removed_notification_ids) const {
  auto total_size = group.notifications.size();
  CHECK(removed_notification_ids.size() <= max_notification_group_size_);
  auto removed_size = min(total_size, max_notification_group_size_ - removed_notification_ids.size());
  removed_notification_ids.reserve(removed_size + removed_notification_ids.size());
  for (size_t i = total_size - removed_size; i < total_size; i++) {
    removed_notification_ids.push_back(group.notifications[i].notification_id.get());
  }

  if (removed_notification_ids.empty()) {
    return nullptr;
  }
  return td_api::make_object<td_api::updateNotificationGroup>(
      group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(),
      group_key.dialog_id.get(), true, group.total_count, vector<td_api::object_ptr<td_api::notification>>(),
      std::move(removed_notification_ids));
}

void NotificationManager::send_remove_group_update(const NotificationGroupKey &group_key,
                                                   const NotificationGroup &group,
                                                   vector<int32> &&removed_notification_ids) {
  VLOG(notifications) << "Remove " << group_key.group_id;
  auto update = get_remove_group_update(group_key, group, std::move(removed_notification_ids));
  if (update != nullptr) {
    add_update_notification_group(std::move(update));
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
        group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(), 0, true,
        group.total_count, std::move(added_notifications), vector<int32>()));
  }
}

void NotificationManager::flush_pending_notifications(NotificationGroupId group_id) {
  auto group_it = get_group(group_id);
  if (group_it == groups_.end()) {
    return;
  }

  if (group_it->second.pending_notifications.empty()) {
    return;
  }

  auto group_key = group_it->first;
  auto group = std::move(group_it->second);

  delete_group(std::move(group_it));

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
    group.total_count += narrow_cast<int32>(group.pending_notifications.size());
    for (auto &pending_notification : group.pending_notifications) {
      group.notifications.emplace_back(pending_notification.notification_id, pending_notification.date,
                                       std::move(pending_notification.type));
    }
  } else {
    if (!was_updated) {
      if (last_group_key.last_notification_date != 0) {
        // need to remove last notification group to not exceed max_notification_group_count_
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
  on_pending_notification_update_count_changed(-1, group_id.get(), "flush_pending_notifications");
  // if we can delete a lot of notifications simultaneously
  if (group.notifications.size() > keep_notification_group_size_ + EXTRA_GROUP_SIZE &&
      group.type != NotificationGroupType::Calls) {
    // keep only keep_notification_group_size_ last notifications in memory
    group.notifications.erase(group.notifications.begin(), group.notifications.end() - keep_notification_group_size_);
    group.is_loaded_from_database = false;
  }

  add_group(std::move(final_group_key), std::move(group));
}

void NotificationManager::flush_all_pending_notifications() {
  std::multimap<int32, NotificationGroupId> group_ids;
  for (auto &group_it : groups_) {
    if (!group_it.second.pending_notifications.empty()) {
      group_ids.emplace(group_it.second.pending_notifications.back().date, group_it.first.group_id);
    }
  }

  // flush groups in order of last notification date
  VLOG(notifications) << "Flush pending notifications in " << group_ids.size() << " notification groups";
  for (auto &it : group_ids) {
    flush_pending_notifications_timeout_.cancel_timeout(it.second.get());
    flush_pending_notifications(it.second);
  }
}

void NotificationManager::edit_notification(NotificationGroupId group_id, NotificationId notification_id,
                                            unique_ptr<NotificationType> type) {
  if (is_disabled() || max_notification_group_count_ == 0) {
    return;
  }
  if (!group_id.is_valid()) {
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
      if (i + max_notification_group_size_ >= group.notifications.size() &&
          !(get_last_updated_group_key() < group_it->first)) {
        CHECK(group_it->first.last_notification_date != 0);
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
    vector<int32> &&removed_notification_ids, bool force_update) {
  VLOG(notifications) << "In on_notifications_removed for " << group_it->first.group_id << " with "
                      << added_notifications.size() << " added notifications and " << removed_notification_ids.size()
                      << " removed notifications, new total_count = " << group_it->second.total_count;
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
    delete_group(std::move(group_it));
  }

  auto last_group_key = get_last_updated_group_key();
  bool was_updated = false;
  bool is_updated = false;
  if (is_position_changed) {
    was_updated = group_key.last_notification_date != 0 && group_key < last_group_key;
    is_updated = final_group_key.last_notification_date != 0 && final_group_key < last_group_key;
  } else {
    was_updated = is_updated = group_key.last_notification_date != 0 && !(last_group_key < group_key);
  }

  if (!was_updated) {
    CHECK(!is_updated);
    if (final_group_key.last_notification_date == 0 && group.total_count == 0) {
      // send update about empty invisible group anyway
      add_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
          group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(), 0, true,
          0, vector<td_api::object_ptr<td_api::notification>>(), vector<int32>()));
    } else {
      VLOG(notifications) << "There is no need to send updateNotificationGroup about " << group_key.group_id;
    }
  } else {
    if (is_updated) {
      // group is still visible
      add_update_notification_group(td_api::make_object<td_api::updateNotificationGroup>(
          group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(), 0, true,
          group.total_count, std::move(added_notifications), std::move(removed_notification_ids)));
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
    add_group(std::move(final_group_key), std::move(group));

    last_group_key = get_last_updated_group_key();
  } else {
    group_it->second = std::move(group);
  }

  if (force_update) {
    auto id = group_key.group_id.get();
    flush_pending_updates_timeout_.cancel_timeout(id);
    flush_pending_updates(id, "on_notifications_removed");
  }

  if (last_loaded_notification_group_key_ < last_group_key) {
    load_message_notification_groups_from_database(td::max(static_cast<int32>(max_notification_group_count_), 10) / 2,
                                                   true);
  }
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
                                              bool is_permanent, bool force_update, Promise<Unit> &&promise) {
  if (!group_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification group identifier is invalid"));
  }
  if (!notification_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }

  if (is_disabled() || max_notification_group_count_ == 0) {
    return promise.set_value(Unit());
  }

  VLOG(notifications) << "Remove " << notification_id << " from " << group_id
                      << " with force_update = " << force_update;

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    return promise.set_value(Unit());
  }

  if (!is_permanent && group_it->second.type != NotificationGroupType::Calls) {
    td_->messages_manager_->remove_message_notification(group_it->first.dialog_id, group_id, notification_id);
  }

  for (auto it = group_it->second.pending_notifications.begin(); it != group_it->second.pending_notifications.end();
       ++it) {
    if (it->notification_id == notification_id) {
      // notification is still pending, just delete it
      group_it->second.pending_notifications.erase(it);
      if (group_it->second.pending_notifications.empty()) {
        group_it->second.pending_notifications_flush_time = 0;
        flush_pending_notifications_timeout_.cancel_timeout(group_id.get());
        on_pending_notification_update_count_changed(-1, group_id.get(), "remove_notification");
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

  bool is_total_count_changed = false;
  if ((group_it->second.type != NotificationGroupType::Calls && is_permanent) ||
      (group_it->second.type == NotificationGroupType::Calls && is_found)) {
    if (group_it->second.total_count == 0) {
      LOG(ERROR) << "Total notification count became negative in " << group_id << " after removing " << notification_id;
    } else {
      group_it->second.total_count--;
      is_total_count_changed = true;
    }
  }
  if (is_found) {
    group_it->second.notifications.erase(group_it->second.notifications.begin() + notification_pos);
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
      load_message_notifications_from_database(group_it->first, group_it->second, keep_notification_group_size_);
    }
  }

  if (is_total_count_changed || !removed_notification_ids.empty()) {
    on_notifications_removed(std::move(group_it), std::move(added_notifications), std::move(removed_notification_ids),
                             force_update);
  }

  remove_added_notifications_from_pending_updates(
      group_id, [notification_id](const td_api::object_ptr<td_api::notification> &notification) {
        return notification->id_ == notification_id.get();
      });

  promise.set_value(Unit());
}

void NotificationManager::remove_notification_group(NotificationGroupId group_id, NotificationId max_notification_id,
                                                    MessageId max_message_id, int32 new_total_count, bool force_update,
                                                    Promise<Unit> &&promise) {
  if (!group_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Group identifier is invalid"));
  }
  if (!max_notification_id.is_valid() && !max_message_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Notification identifier is invalid"));
  }

  if (is_disabled() || max_notification_group_count_ == 0) {
    return promise.set_value(Unit());
  }

  VLOG(notifications) << "Remove " << group_id << " up to " << max_notification_id << " or " << max_message_id
                      << " with new_total_count = " << new_total_count << " and force_update = " << force_update;

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    VLOG(notifications) << "Can't find " << group_id;
    return promise.set_value(Unit());
  }

  if (max_notification_id.is_valid()) {
    if (max_notification_id.get() > current_notification_id_.get()) {
      max_notification_id = current_notification_id_;
    }
    if (group_it->second.type != NotificationGroupType::Calls) {
      td_->messages_manager_->remove_message_notifications(group_it->first.dialog_id, group_id, max_notification_id);
    }
  }

  auto pending_delete_end = group_it->second.pending_notifications.begin();
  for (auto it = group_it->second.pending_notifications.begin(); it != group_it->second.pending_notifications.end();
       ++it) {
    if (it->notification_id.get() <= max_notification_id.get() ||
        (max_message_id.is_valid() && it->type->get_message_id().get() <= max_message_id.get())) {
      pending_delete_end = it + 1;
    }
  }
  if (pending_delete_end != group_it->second.pending_notifications.begin()) {
    group_it->second.pending_notifications.erase(group_it->second.pending_notifications.begin(), pending_delete_end);
    if (group_it->second.pending_notifications.empty()) {
      group_it->second.pending_notifications_flush_time = 0;
      flush_pending_notifications_timeout_.cancel_timeout(group_id.get());
      on_pending_notification_update_count_changed(-1, group_id.get(), "remove_notification_group");
    }
  }
  if (new_total_count != -1) {
    new_total_count -= static_cast<int32>(group_it->second.pending_notifications.size());
    if (new_total_count < 0) {
      LOG(ERROR) << "Have wrong new_total_count " << new_total_count << " + "
                 << group_it->second.pending_notifications.size();
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

  VLOG(notifications) << "Need to delete " << notification_delete_end << " from "
                      << group_it->second.notifications.size() << " notifications";
  if (is_found) {
    group_it->second.notifications.erase(group_it->second.notifications.begin(),
                                         group_it->second.notifications.begin() + notification_delete_end);
  }
  if (group_it->second.type == NotificationGroupType::Calls) {
    new_total_count = static_cast<int32>(group_it->second.notifications.size());
  }
  if (group_it->second.total_count == new_total_count) {
    new_total_count = -1;
  }
  if (new_total_count != -1) {
    group_it->second.total_count = new_total_count;
  }

  if (new_total_count != -1 || !removed_notification_ids.empty()) {
    on_notifications_removed(std::move(group_it), vector<td_api::object_ptr<td_api::notification>>(),
                             std::move(removed_notification_ids), force_update);
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

void NotificationManager::set_notification_total_count(NotificationGroupId group_id, int32 new_total_count) {
  if (!group_id.is_valid()) {
    return;
  }
  if (is_disabled() || max_notification_group_count_ == 0) {
    return;
  }

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    VLOG(notifications) << "Can't find " << group_id;
    return;
  }

  new_total_count -= static_cast<int32>(group_it->second.pending_notifications.size());
  if (new_total_count < 0) {
    LOG(ERROR) << "Have wrong new_total_count " << new_total_count << " after removing "
               << group_it->second.pending_notifications.size() << " pending notifications";
    return;
  }
  if (new_total_count < static_cast<int32>(group_it->second.notifications.size())) {
    LOG(ERROR) << "Have wrong new_total_count " << new_total_count << " less than number of known notifications "
               << group_it->second.notifications.size();
    return;
  }

  CHECK(group_it->second.type != NotificationGroupType::Calls);
  if (group_it->second.total_count == new_total_count) {
    return;
  }

  VLOG(notifications) << "Set total_count in " << group_id << " to " << new_total_count;
  group_it->second.total_count = new_total_count;

  on_notifications_removed(std::move(group_it), vector<td_api::object_ptr<td_api::notification>>(), vector<int32>(),
                           false);
}

vector<MessageId> NotificationManager::get_notification_group_message_ids(NotificationGroupId group_id) {
  CHECK(group_id.is_valid());
  if (is_disabled() || max_notification_group_count_ == 0) {
    return {};
  }

  auto group_it = get_group_force(group_id);
  if (group_it == groups_.end()) {
    return {};
  }

  vector<MessageId> message_ids;
  for (auto &notification : group_it->second.notifications) {
    auto message_id = notification.type->get_message_id();
    if (message_id.is_valid()) {
      message_ids.push_back(message_id);
    }
  }
  for (auto &notification : group_it->second.pending_notifications) {
    auto message_id = notification.type->get_message_id();
    if (message_id.is_valid()) {
      message_ids.push_back(message_id);
    }
  }

  return message_ids;
}

NotificationGroupId NotificationManager::get_call_notification_group_id(DialogId dialog_id) {
  auto it = dialog_id_to_call_notification_group_id_.find(dialog_id);
  if (it != dialog_id_to_call_notification_group_id_.end()) {
    return it->second;
  }

  if (available_call_notification_group_ids_.empty()) {
    // need to reserve new group_id for calls
    if (call_notification_group_ids_.size() >= MAX_CALL_NOTIFICATION_GROUPS) {
      return {};
    }
    NotificationGroupId last_group_id;
    if (!call_notification_group_ids_.empty()) {
      last_group_id = call_notification_group_ids_.back();
    }
    NotificationGroupId next_notification_group_id;
    do {
      next_notification_group_id = get_next_notification_group_id();
      if (!next_notification_group_id.is_valid()) {
        return {};
      }
    } while (last_group_id.get() >= next_notification_group_id.get());  // just in case
    VLOG(notifications) << "Add call " << next_notification_group_id;

    call_notification_group_ids_.push_back(next_notification_group_id);
    auto call_notification_group_ids_string = implode(
        transform(call_notification_group_ids_, [](NotificationGroupId group_id) { return to_string(group_id.get()); }),
        ',');
    G()->td_db()->get_binlog_pmc()->set("notification_call_group_ids", call_notification_group_ids_string);
    available_call_notification_group_ids_.insert(next_notification_group_id);
  }

  auto available_it = available_call_notification_group_ids_.begin();
  auto group_id = *available_it;
  available_call_notification_group_ids_.erase(available_it);
  dialog_id_to_call_notification_group_id_[dialog_id] = group_id;
  return group_id;
}

void NotificationManager::add_call_notification(DialogId dialog_id, CallId call_id) {
  CHECK(dialog_id.is_valid());
  CHECK(call_id.is_valid());
  if (is_disabled() || max_notification_group_count_ == 0) {
    return;
  }

  auto group_id = get_call_notification_group_id(dialog_id);
  if (!group_id.is_valid()) {
    VLOG(notifications) << "Ignore notification about " << call_id << " in " << dialog_id;
    return;
  }

  G()->td().get_actor_unsafe()->messages_manager_->force_create_dialog(dialog_id, "add_call_notification");

  auto &active_notifications = active_call_notifications_[dialog_id];
  if (active_notifications.size() >= MAX_CALL_NOTIFICATIONS) {
    VLOG(notifications) << "Ignore notification about " << call_id << " in " << dialog_id << " and " << group_id;
    return;
  }

  auto notification_id = get_next_notification_id();
  if (!notification_id.is_valid()) {
    return;
  }
  active_notifications.push_back(ActiveCallNotification{call_id, notification_id});

  add_notification(group_id, NotificationGroupType::Calls, dialog_id, G()->unix_time() + 120, dialog_id, false, 0,
                   notification_id, create_new_call_notification(call_id));
}

void NotificationManager::remove_call_notification(DialogId dialog_id, CallId call_id) {
  CHECK(dialog_id.is_valid());
  CHECK(call_id.is_valid());
  if (is_disabled() || max_notification_group_count_ == 0) {
    return;
  }

  auto group_id_it = dialog_id_to_call_notification_group_id_.find(dialog_id);
  if (group_id_it == dialog_id_to_call_notification_group_id_.end()) {
    VLOG(notifications) << "Ignore removing notification about " << call_id << " in " << dialog_id;
    return;
  }
  auto group_id = group_id_it->second;
  CHECK(group_id.is_valid());

  auto &active_notifications = active_call_notifications_[dialog_id];
  for (auto it = active_notifications.begin(); it != active_notifications.end(); ++it) {
    if (it->call_id == call_id) {
      remove_notification(group_id, it->notification_id, true, true, Promise<Unit>());
      active_notifications.erase(it);
      if (active_notifications.empty()) {
        VLOG(notifications) << "Reuse call " << group_id;
        active_call_notifications_.erase(dialog_id);
        available_call_notification_group_ids_.insert(group_id);
        dialog_id_to_call_notification_group_id_.erase(dialog_id);

        flush_pending_notifications_timeout_.cancel_timeout(group_id.get());
        flush_pending_notifications(group_id);
        flush_pending_updates_timeout_.cancel_timeout(group_id.get());
        flush_pending_updates(group_id.get(), "reuse call group_id");

        auto group_it = get_group(group_id);
        CHECK(group_it->first.dialog_id == dialog_id);
        CHECK(group_it->first.last_notification_date == 0);
        CHECK(group_it->second.total_count == 0);
        CHECK(group_it->second.notifications.empty());
        CHECK(group_it->second.pending_notifications.empty());
        CHECK(group_it->second.type == NotificationGroupType::Calls);
        CHECK(!group_it->second.is_being_loaded_from_database);
        CHECK(pending_updates_.count(group_id.get()) == 0);
        delete_group(std::move(group_it));
      }
      return;
    }
  }

  VLOG(notifications) << "Failed to find " << call_id << " in " << dialog_id << " and " << group_id;
}

void NotificationManager::on_notification_group_count_max_changed(bool send_updates) {
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
  if (send_updates) {
    flush_all_notifications();

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

      if (group_key.last_notification_date == 0) {
        break;
      }

      if (is_increased) {
        send_add_group_update(group_key, group);
      } else {
        send_remove_group_update(group_key, group, vector<int32>());
      }
    }

    flush_all_pending_updates(true, "on_notification_group_size_max_changed end");

    if (new_max_notification_group_count == 0) {
      last_loaded_notification_group_key_ = NotificationGroupKey();
      last_loaded_notification_group_key_.last_notification_date = std::numeric_limits<int32>::max();
      CHECK(pending_updates_.empty());
      groups_.clear();
      group_keys_.clear();
    }
  }

  max_notification_group_count_ = new_max_notification_group_count_size_t;
  if (is_increased && last_loaded_notification_group_key_ < get_last_updated_group_key()) {
    load_message_notification_groups_from_database(td::max(new_max_notification_group_count, 5), true);
  }
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

  auto new_keep_notification_group_size =
      new_max_notification_group_size_size_t +
      clamp(new_max_notification_group_size_size_t, EXTRA_GROUP_SIZE / 2, EXTRA_GROUP_SIZE);

  VLOG(notifications) << "Change max notification group size from " << max_notification_group_size_ << " to "
                      << new_max_notification_group_size;

  if (max_notification_group_size_ != 0) {
    flush_all_notifications();

    size_t left = max_notification_group_count_;
    for (auto it = groups_.begin(); it != groups_.end() && left > 0; ++it, left--) {
      auto &group_key = it->first;
      auto &group = it->second;
      CHECK(group.pending_notifications.empty());
      CHECK(pending_updates_.count(group_key.group_id.get()) == 0);

      if (group_key.last_notification_date == 0) {
        break;
      }

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
        if (new_max_notification_group_size_size_t > notification_count) {
          load_message_notifications_from_database(group_key, group, new_keep_notification_group_size);
        }
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
        if (added_notifications.empty()) {
          continue;
        }
      }
      if (!is_destroyed_) {
        auto update = td_api::make_object<td_api::updateNotificationGroup>(
            group_key.group_id.get(), get_notification_group_type_object(group.type), group_key.dialog_id.get(),
            group_key.dialog_id.get(), true, group.total_count, std::move(added_notifications),
            std::move(removed_notification_ids));
        VLOG(notifications) << "Send " << as_notification_update(update.get());
        send_closure(G()->td(), &Td::send_update, std::move(update));
      }
    }
  }

  max_notification_group_size_ = new_max_notification_group_size_size_t;
  keep_notification_group_size_ = new_keep_notification_group_size;
}

void NotificationManager::on_online_cloud_timeout_changed() {
  if (is_disabled()) {
    return;
  }

  online_cloud_timeout_ms_ =
      G()->shared_config().get_option_integer("online_cloud_timeout_ms", DEFAULT_ONLINE_CLOUD_TIMEOUT_MS);
  VLOG(notifications) << "Set online_cloud_timeout_ms to " << online_cloud_timeout_ms_;
}

void NotificationManager::on_notification_cloud_delay_changed() {
  if (is_disabled()) {
    return;
  }

  notification_cloud_delay_ms_ =
      G()->shared_config().get_option_integer("notification_cloud_delay_ms", DEFAULT_ONLINE_CLOUD_DELAY_MS);
  VLOG(notifications) << "Set notification_cloud_delay_ms to " << notification_cloud_delay_ms_;
}

void NotificationManager::on_notification_default_delay_changed() {
  if (is_disabled()) {
    return;
  }

  notification_default_delay_ms_ =
      G()->shared_config().get_option_integer("notification_default_delay_ms", DEFAULT_DEFAULT_DELAY_MS);
  VLOG(notifications) << "Set notification_default_delay_ms to " << notification_default_delay_ms_;
}

void NotificationManager::on_disable_contact_registered_notifications_changed() {
  if (is_disabled()) {
    return;
  }

  auto is_disabled = G()->shared_config().get_option_boolean("disable_contact_registered_notifications");

  if (is_disabled == disable_contact_registered_notifications_) {
    return;
  }

  disable_contact_registered_notifications_ = is_disabled;
  if (contact_registered_notifications_sync_state_ == SyncState::Completed) {
    run_contact_registered_notifications_sync();
  }
}

void NotificationManager::on_get_disable_contact_registered_notifications(bool is_disabled) {
  if (disable_contact_registered_notifications_ == is_disabled) {
    return;
  }
  disable_contact_registered_notifications_ = is_disabled;

  if (is_disabled) {
    G()->shared_config().set_option_boolean("disable_contact_registered_notifications", is_disabled);
  } else {
    G()->shared_config().set_option_empty("disable_contact_registered_notifications");
  }
}

void NotificationManager::set_contact_registered_notifications_sync_state(SyncState new_state) {
  if (is_disabled()) {
    return;
  }

  contact_registered_notifications_sync_state_ = new_state;
  string value;
  value += static_cast<char>(static_cast<int32>(new_state) + '0');
  value += static_cast<char>(static_cast<int32>(disable_contact_registered_notifications_) + '0');
  G()->td_db()->get_binlog_pmc()->set(get_is_contact_registered_notifications_synchronized_key(), value);
}

void NotificationManager::run_contact_registered_notifications_sync() {
  if (is_disabled()) {
    return;
  }

  auto is_disabled = disable_contact_registered_notifications_;
  if (contact_registered_notifications_sync_state_ == SyncState::NotSynced && !is_disabled) {
    set_contact_registered_notifications_sync_state(SyncState::Completed);
    return;
  }
  if (contact_registered_notifications_sync_state_ != SyncState::Pending) {
    set_contact_registered_notifications_sync_state(SyncState::Pending);
  }

  VLOG(notifications) << "Send SetContactSignUpNotificationQuery with " << is_disabled;
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), is_disabled](Result<Unit> result) {
    send_closure(actor_id, &NotificationManager::on_contact_registered_notifications_sync, is_disabled,
                 std::move(result));
  });
  td_->create_handler<SetContactSignUpNotificationQuery>(std::move(promise))->send(is_disabled);
}

void NotificationManager::on_contact_registered_notifications_sync(bool is_disabled, Result<Unit> result) {
  CHECK(contact_registered_notifications_sync_state_ == SyncState::Pending);
  if (is_disabled != disable_contact_registered_notifications_) {
    return run_contact_registered_notifications_sync();
  }
  if (result.is_ok()) {
    // everything is synchronized
    set_contact_registered_notifications_sync_state(SyncState::Completed);
  } else {
    // let's resend the query forever
    run_contact_registered_notifications_sync();
  }
}

void NotificationManager::get_disable_contact_registered_notifications(Promise<Unit> &&promise) {
  if (is_disabled()) {
    promise.set_value(Unit());
    return;
  }

  td_->create_handler<GetContactSignUpNotificationQuery>(std::move(promise))->send();
}

void NotificationManager::process_push_notification(string payload, Promise<Unit> &&promise) {
  if (is_disabled() || payload == "{}") {
    promise.set_value(Unit());
    return;
  }

  auto r_receiver_id = get_push_receiver_id(payload);
  if (r_receiver_id.is_error()) {
    VLOG(notifications) << "Failed to get push notification receiver from \"" << format::escaped(payload) << '"';
    promise.set_error(r_receiver_id.move_as_error());
    return;
  }

  auto receiver_id = r_receiver_id.move_as_ok();
  VLOG(notifications) << "Process push notification \"" << format::escaped(payload)
                      << "\" with receiver_id = " << receiver_id;

  auto encryption_keys = td_->device_token_manager_->get_actor_unsafe()->get_encryption_keys();
  for (auto &key : encryption_keys) {
    // VLOG(notifications) << "Have key " << key.first << ": \"" << format::escaped(key.second) << '"';
    if (key.first == receiver_id) {
      if (!key.second.empty()) {
        auto r_payload = decrypt_push(key.first, key.second.str(), std::move(payload));
        if (r_payload.is_error()) {
          LOG(ERROR) << "Failed to decrypt push: " << r_payload.error();
          promise.set_error(Status::Error(400, "Failed to decrypt push payload"));
          return;
        }
        payload = r_payload.move_as_ok();
      }
      receiver_id = 0;
      break;
    }
  }
  if (receiver_id == 0 || receiver_id == G()->get_my_id()) {
    auto status = process_push_notification_payload(payload);
    if (status.is_error()) {
      LOG(ERROR) << "Receive error " << status << ", while parsing push payload " << payload;
    }
    promise.set_value(Unit());
    return;
  }

  VLOG(notifications) << "Failed to process push notification";
  promise.set_value(Unit());
}

string NotificationManager::convert_loc_key(const string &loc_key) {
  if (loc_key == "MESSAGES") {
    return loc_key;
  }
  switch (loc_key[8]) {
    case 'A':
      if (loc_key == "PINNED_GAME") {
        return "PINNED_MESSAGE_GAME";
      }
      if (loc_key == "CHAT_CREATED") {
        return "MESSAGE_BASIC_GROUP_CHAT_CREATE";
      }
      if (loc_key == "MESSAGE_AUDIO") {
        return "MESSAGE_VOICE_NOTE";
      }
      break;
    case 'C':
      if (loc_key == "MESSAGE_CONTACT") {
        return "MESSAGE_CONTACT";
      }
      break;
    case 'D':
      if (loc_key == "MESSAGE_DOC") {
        return "MESSAGE_DOCUMENT";
      }
      break;
    case 'E':
      if (loc_key == "PINNED_GEO") {
        return "PINNED_MESSAGE_LOCATION";
      }
      if (loc_key == "PINNED_GEOLIVE") {
        return "PINNED_MESSAGE_LIVE_LOCATION";
      }
      if (loc_key == "CHAT_DELETE_MEMBER") {
        return "MESSAGE_CHAT_DELETE_MEMBER";
      }
      if (loc_key == "CHAT_DELETE_YOU") {
        return "MESSAGE_CHAT_DELETE_MEMBER_YOU";
      }
      if (loc_key == "PINNED_TEXT") {
        return "PINNED_MESSAGE_TEXT";
      }
      break;
    case 'F':
      if (loc_key == "MESSAGE_FWDS") {
        return "MESSAGE_FORWARDS";
      }
      break;
    case 'G':
      if (loc_key == "MESSAGE_GAME") {
        return "MESSAGE_GAME";
      }
      if (loc_key == "MESSAGE_GEO") {
        return "MESSAGE_LOCATION";
      }
      if (loc_key == "MESSAGE_GEOLIVE") {
        return "MESSAGE_LIVE_LOCATION";
      }
      if (loc_key == "MESSAGE_GIF") {
        return "MESSAGE_ANIMATION";
      }
      break;
    case 'H':
      if (loc_key == "PINNED_PHOTO") {
        return "PINNED_MESSAGE_PHOTO";
      }
      break;
    case 'I':
      if (loc_key == "PINNED_VIDEO") {
        return "PINNED_MESSAGE_VIDEO";
      }
      if (loc_key == "PINNED_GIF") {
        return "PINNED_MESSAGE_ANIMATION";
      }
      if (loc_key == "MESSAGE_INVOICE") {
        return "MESSAGE_INVOICE";
      }
      break;
    case 'J':
      if (loc_key == "CONTACT_JOINED") {
        return "MESSAGE_CONTACT_REGISTERED";
      }
      break;
    case 'L':
      if (loc_key == "CHAT_TITLE_EDITED") {
        return "MESSAGE_CHAT_CHANGE_TITLE";
      }
      break;
    case 'N':
      if (loc_key == "CHAT_JOINED") {
        return "MESSAGE_CHAT_JOIN_BY_LINK";
      }
      if (loc_key == "MESSAGE_NOTEXT") {
        return "MESSAGE";
      }
      if (loc_key == "PINNED_INVOICE") {
        return "PINNED_MESSAGE_INVOICE";
      }
      break;
    case 'O':
      if (loc_key == "PINNED_DOC") {
        return "PINNED_MESSAGE_DOCUMENT";
      }
      if (loc_key == "PINNED_POLL") {
        return "PINNED_MESSAGE_POLL";
      }
      if (loc_key == "PINNED_CONTACT") {
        return "PINNED_MESSAGE_CONTACT";
      }
      if (loc_key == "PINNED_NOTEXT") {
        return "PINNED_MESSAGE";
      }
      if (loc_key == "PINNED_ROUND") {
        return "PINNED_MESSAGE_VIDEO_NOTE";
      }
      break;
    case 'P':
      if (loc_key == "MESSAGE_PHOTO") {
        return "MESSAGE_PHOTO";
      }
      if (loc_key == "MESSAGE_PHOTOS") {
        return "MESSAGE_PHOTOS";
      }
      if (loc_key == "MESSAGE_PHOTO_SECRET") {
        return "MESSAGE_SECRET_PHOTO";
      }
      if (loc_key == "MESSAGE_POLL") {
        return "MESSAGE_POLL";
      }
      break;
    case 'R':
      if (loc_key == "MESSAGE_ROUND") {
        return "MESSAGE_VIDEO_NOTE";
      }
      break;
    case 'S':
      if (loc_key == "MESSAGE_SCREENSHOT") {
        return "MESSAGE_SCREENSHOT_TAKEN";
      }
      if (loc_key == "MESSAGE_STICKER") {
        return "MESSAGE_STICKER";
      }
      break;
    case 'T':
      if (loc_key == "CHAT_LEFT") {
        return "MESSAGE_CHAT_DELETE_MEMBER_LEFT";
      }
      if (loc_key == "MESSAGE_TEXT") {
        return "MESSAGE_TEXT";
      }
      if (loc_key == "PINNED_STICKER") {
        return "PINNED_MESSAGE_STICKER";
      }
      if (loc_key == "CHAT_PHOTO_EDITED") {
        return "MESSAGE_CHAT_CHANGE_PHOTO";
      }
      break;
    case 'U':
      if (loc_key == "PINNED_AUDIO") {
        return "PINNED_MESSAGE_VOICE_NOTE";
      }
      if (loc_key == "CHAT_RETURNED") {
        return "MESSAGE_CHAT_ADD_MEMBERS_RETURNED";
      }
      break;
    case 'V':
      if (loc_key == "MESSAGE_VIDEO") {
        return "MESSAGE_VIDEO";
      }
      if (loc_key == "MESSAGE_VIDEO_SECRET") {
        return "MESSAGE_SECRET_VIDEO";
      }
      break;
    case '_':
      if (loc_key == "CHAT_ADD_MEMBER") {
        return "MESSAGE_CHAT_ADD_MEMBERS";
      }
      if (loc_key == "CHAT_ADD_YOU") {
        return "MESSAGE_CHAT_ADD_MEMBERS_YOU";
      }
      break;
  }
  return string();
}

Status NotificationManager::process_push_notification_payload(string payload) {
  VLOG(notifications) << "Process push notification payload " << payload;
  auto r_json_value = json_decode(payload);
  if (r_json_value.is_error()) {
    return Status::Error("Failed to parse payload as JSON object");
  }

  auto json_value = r_json_value.move_as_ok();
  if (json_value.type() != JsonValue::Type::Object) {
    return Status::Error("Expected a JSON object as push payload");
  }

  string loc_key;
  JsonObject custom;
  string announcement_message_text;
  vector<string> loc_args;
  string sender_name;
  int32 sent_date = G()->unix_time();
  bool is_silent = false;
  for (auto &field_value : json_value.get_object()) {
    if (field_value.first == "loc_key") {
      if (field_value.second.type() != JsonValue::Type::String) {
        return Status::Error("Expected loc_key as a String");
      }
      loc_key = field_value.second.get_string().str();
    } else if (field_value.first == "loc_args") {
      if (field_value.second.type() != JsonValue::Type::Array) {
        return Status::Error("Expected loc_args as an Array");
      }
      loc_args.reserve(field_value.second.get_array().size());
      for (auto &arg : field_value.second.get_array()) {
        if (arg.type() != JsonValue::Type::String) {
          return Status::Error("Expected loc_arg as a String");
        }
        loc_args.push_back(arg.get_string().str());
      }
    } else if (field_value.first == "custom") {
      if (field_value.second.type() != JsonValue::Type::Object) {
        return Status::Error("Expected custom as an Object");
      }
      custom = std::move(field_value.second.get_object());
    } else if (field_value.first == "message") {
      if (field_value.second.type() != JsonValue::Type::String) {
        return Status::Error("Expected announcement message text as a String");
      }
      announcement_message_text = field_value.second.get_string().str();
    } else if (field_value.first == "google.sent_time") {
      TRY_RESULT(google_sent_time, get_json_object_long_field(json_value.get_object(), "google.sent_time"));
      google_sent_time /= 1000;
      if (sent_date - 86400 <= google_sent_time && google_sent_time <= sent_date + 5) {
        sent_date = narrow_cast<int32>(google_sent_time);
      }
    } else if (field_value.first == "google.notification.sound" && field_value.second.type() != JsonValue::Type::Null) {
      if (field_value.second.type() != JsonValue::Type::String) {
        return Status::Error("Expected notification sound as a String");
      }
      is_silent = field_value.second.get_string().empty();
    }
  }
  if (!clean_input_string(loc_key)) {
    return Status::Error(PSLICE() << "Receive invalid loc_key " << format::escaped(loc_key));
  }
  for (auto &loc_arg : loc_args) {
    if (!clean_input_string(loc_arg)) {
      return Status::Error(PSLICE() << "Receive invalid loc_arg " << format::escaped(loc_arg));
    }
  }

  if (loc_key == "MESSAGE_ANNOUNCEMENT") {
    if (announcement_message_text.empty()) {
      return Status::Error("Have empty announcement message text");
    }
    TRY_RESULT(announcement_id, get_json_object_int_field(custom, "announcement"));
    auto &date = announcement_id_date_[announcement_id];
    auto now = G()->unix_time();
    if (date >= now - ANNOUNCEMENT_ID_CACHE_TIME) {
      VLOG(notifications) << "Ignore duplicate announcement " << announcement_id;
      return Status::OK();
    }
    date = now;

    auto update = telegram_api::make_object<telegram_api::updateServiceNotification>(
        telegram_api::updateServiceNotification::INBOX_DATE_MASK, false, G()->unix_time(), string(),
        announcement_message_text, nullptr, vector<telegram_api::object_ptr<telegram_api::MessageEntity>>());
    send_closure(G()->messages_manager(), &MessagesManager::on_update_service_notification, std::move(update), false);
    save_announcement_ids();
    return Status::OK();
  }
  if (!announcement_message_text.empty()) {
    LOG(ERROR) << "Have non-empty announcement message text with loc_key = " << loc_key;
  }

  if (loc_key == "DC_UPDATE") {
    TRY_RESULT(dc_id, get_json_object_int_field(custom, "dc", false));
    TRY_RESULT(addr, get_json_object_string_field(custom, "addr", false));
    if (!DcId::is_valid(dc_id)) {
      return Status::Error("Invalid datacenter ID");
    }
    if (!clean_input_string(addr)) {
      return Status::Error(PSLICE() << "Receive invalid addr " << format::escaped(addr));
    }
    send_closure(G()->connection_creator(), &ConnectionCreator::on_dc_update, DcId::internal(dc_id), std::move(addr),
                 Promise<Unit>());
    return Status::OK();
  }

  if (loc_key == "LOCKED_MESSAGE") {
    return Status::OK();
  }

  if (loc_key == "AUTH_REGION" || loc_key == "AUTH_UNKNOWN") {
    // TODO
    return Status::OK();
  }

  DialogId dialog_id;
  if (has_json_object_field(custom, "from_id")) {
    TRY_RESULT(user_id_int, get_json_object_int_field(custom, "from_id"));
    UserId user_id(user_id_int);
    if (!user_id.is_valid()) {
      return Status::Error("Receive invalid user_id");
    }
    dialog_id = DialogId(user_id);
  }
  if (has_json_object_field(custom, "chat_id")) {
    TRY_RESULT(chat_id_int, get_json_object_int_field(custom, "chat_id"));
    ChatId chat_id(chat_id_int);
    if (!chat_id.is_valid()) {
      return Status::Error("Receive invalid chat_id");
    }
    dialog_id = DialogId(chat_id);
  }
  if (has_json_object_field(custom, "channel_id")) {
    TRY_RESULT(channel_id_int, get_json_object_int_field(custom, "channel_id"));
    ChannelId channel_id(channel_id_int);
    if (!channel_id.is_valid()) {
      return Status::Error("Receive invalid channel_id");
    }
    dialog_id = DialogId(channel_id);
  }
  if (has_json_object_field(custom, "encryption_id")) {
    TRY_RESULT(secret_chat_id_int, get_json_object_int_field(custom, "encryption_id"));
    SecretChatId secret_chat_id(secret_chat_id_int);
    if (!secret_chat_id.is_valid()) {
      return Status::Error("Receive invalid secret_chat_id");
    }
    dialog_id = DialogId(secret_chat_id);
  }
  if (!dialog_id.is_valid()) {
    // TODO if (loc_key == "ENCRYPTED_MESSAGE") ?
    return Status::Error("Can't find dialog_id");
  }

  if (loc_key.empty()) {
    if (dialog_id.get_type() == DialogType::SecretChat) {
      return Status::Error("Receive read history in a secret chat");
    }

    TRY_RESULT(max_id, get_json_object_int_field(custom, "max_id"));
    ServerMessageId max_server_message_id(max_id);
    if (!max_server_message_id.is_valid()) {
      return Status::Error("Receive invalid max_id");
    }

    send_closure(G()->messages_manager(), &MessagesManager::read_history_inbox, dialog_id,
                 MessageId(max_server_message_id), -1, "process_push_notification_payload");
    return Status::OK();
  }

  TRY_RESULT(msg_id, get_json_object_int_field(custom, "msg_id"));
  ServerMessageId server_message_id(msg_id);
  if (server_message_id != ServerMessageId() && !server_message_id.is_valid()) {
    return Status::Error("Receive invalid msg_id");
  }

  TRY_RESULT(random_id, get_json_object_long_field(custom, "random_id"));

  UserId sender_user_id;
  if (has_json_object_field(custom, "chat_from_id")) {
    TRY_RESULT(sender_user_id_int, get_json_object_int_field(custom, "chat_from_id"));
    sender_user_id = UserId(sender_user_id_int);
    if (!sender_user_id.is_valid()) {
      return Status::Error("Receive invalid chat_from_id");
    }
  } else if (dialog_id.get_type() == DialogType::User) {
    sender_user_id = dialog_id.get_user_id();
  }

  TRY_RESULT(contains_mention_int, get_json_object_int_field(custom, "mention"));
  bool contains_mention = contains_mention_int != 0;

  if (begins_with(loc_key, "CHANNEL_MESSAGE")) {
    if (dialog_id.get_type() != DialogType::Channel) {
      return Status::Error("Receive wrong chat type");
    }
    loc_key = loc_key.substr(8);
  }
  if (begins_with(loc_key, "CHAT_")) {
    auto dialog_type = dialog_id.get_type();
    if (dialog_type != DialogType::Chat && dialog_type != DialogType::Channel) {
      return Status::Error("Receive wrong chat type");
    }

    if (begins_with(loc_key, "CHAT_MESSAGE")) {
      loc_key = loc_key.substr(5);
    }
    if (loc_args.empty()) {
      return Status::Error("Expect sender name as first argument");
    }
    sender_name = std::move(loc_args[0]);
    loc_args.erase(loc_args.begin());
  }
  if (begins_with(loc_key, "MESSAGE") && !server_message_id.is_valid()) {
    return Status::Error("Receive no message ID");
  }
  if (begins_with(loc_key, "ENCRYPT") || random_id != 0) {
    if (dialog_id.get_type() != DialogType::SecretChat) {
      return Status::Error("Receive wrong chat type");
    }
  }
  if (server_message_id.is_valid() && dialog_id.get_type() == DialogType::SecretChat) {
    return Status::Error("Receive message ID in secret chat push");
  }

  if (begins_with(loc_key, "ENCRYPTION_")) {
    // TODO new secret chat notifications
    return Status::OK();
  }

  if (begins_with(loc_key, "PHONE_CALL_")) {
    // TODO phone call request/missed notification
    return Status::OK();
  }

  loc_key = convert_loc_key(loc_key);
  if (loc_key.empty()) {
    return Status::Error("Push type is unknown");
  }

  if (loc_args.empty()) {
    return Status::Error("Expected chat name as next argument");
  }
  if (dialog_id.get_type() == DialogType::User) {
    sender_name = std::move(loc_args[0]);
  }
  // chat title for CHAT_*, CHANNEL_*, ENCRYPTED_MESSAGE and PINNED_*, sender name for MESSAGE_* and CONTACT_JOINED
  loc_args.erase(loc_args.begin());

  return process_message_push_notification(dialog_id, MessageId(server_message_id), random_id, sender_user_id,
                                           std::move(sender_name), sent_date, contains_mention, is_silent,
                                           std::move(loc_key), std::move(loc_args));
}

Status NotificationManager::process_message_push_notification(DialogId dialog_id, MessageId message_id, int64 random_id,
                                                              UserId sender_user_id, string sender_name, int32 date,
                                                              bool contains_mention, bool is_silent, string loc_key,
                                                              vector<string> loc_args) {
  if (loc_args.size() > 1) {
    return Status::Error("Receive too much arguments");
  }

  string arg;
  if (loc_args.size() == 1) {
    arg = std::move(loc_args[0]);
  }

  auto is_pinned = begins_with(loc_key, "PINNED_");
  auto r_info = td_->messages_manager_->get_message_push_notification_info(
      dialog_id, message_id, random_id, sender_user_id, date, contains_mention, is_pinned);
  if (r_info.is_error()) {
    VLOG(notifications) << "Don't need message push notification for " << message_id << "/" << random_id << " from "
                        << dialog_id << ": " << r_info.error();
    return Status::OK();
  }

  auto info = r_info.move_as_ok();
  CHECK(info.group_id.is_valid());

  if (dialog_id.get_type() == DialogType::SecretChat) {
    VLOG(notifications) << "Skep notification in secret " << dialog_id;
    // TODO support secret chat notifications
    // main problem: there is no message_id yet
    return Status::OK();
  }
  CHECK(random_id == 0);

  auto notification_id = get_next_notification_id();
  if (!notification_id.is_valid()) {
    return Status::OK();
  }

  if (sender_user_id.is_valid() && !td_->contacts_manager_->have_user(sender_user_id)) {
    int32 flags = telegram_api::user::FIRST_NAME_MASK | telegram_api::user::MIN_MASK;
    auto user = telegram_api::make_object<telegram_api::user>(
        flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
        false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
        false /*ignored*/, false /*ignored*/, sender_user_id.get(), 0, sender_name, string(), string(), string(),
        nullptr, nullptr, 0, string(), string(), string());
    td_->contacts_manager_->on_get_user(std::move(user), "process_message_push_notification");
  }

  auto group_id = info.group_id;
  auto group_type = info.group_type;
  auto settings_dialog_id = info.settings_dialog_id;
  VLOG(notifications) << "Add message push notification of type " << loc_key << " for " << message_id << "/"
                      << random_id << " in " << dialog_id << ", sent by " << sender_user_id << " at " << date
                      << " with args " << loc_args << " to " << group_id << " of type " << group_type
                      << " with settings from " << settings_dialog_id;

  add_notification(
      group_id, group_type, dialog_id, date, settings_dialog_id, is_silent, 0, notification_id,
      create_new_push_message_notification(sender_user_id, message_id, std::move(loc_key), std::move(arg)));
  return Status::OK();
}

Result<int64> NotificationManager::get_push_receiver_id(string payload) {
  if (payload == "{}") {
    return static_cast<int64>(0);
  }

  auto r_json_value = json_decode(payload);
  if (r_json_value.is_error()) {
    return Status::Error(400, "Failed to parse payload as JSON object");
  }

  auto json_value = r_json_value.move_as_ok();
  if (json_value.type() != JsonValue::Type::Object) {
    return Status::Error(400, "Expected JSON object");
  }

  for (auto &field_value : json_value.get_object()) {
    if (field_value.first == "p") {
      auto encrypted_payload = std::move(field_value.second);
      if (encrypted_payload.type() != JsonValue::Type::String) {
        return Status::Error(400, "Expected encrypted payload as a String");
      }
      Slice data = encrypted_payload.get_string();
      if (data.size() < 12) {
        return Status::Error(400, "Encrypted payload is too small");
      }
      auto r_decoded = base64url_decode(data.substr(0, 12));
      if (r_decoded.is_error()) {
        return Status::Error(400, "Failed to base64url-decode payload");
      }
      CHECK(r_decoded.ok().size() == 9);
      return as<int64>(r_decoded.ok().c_str());
    }
    if (field_value.first == "user_id") {
      auto user_id = std::move(field_value.second);
      if (user_id.type() != JsonValue::Type::String && user_id.type() != JsonValue::Type::Number) {
        return Status::Error(400, "Expected user_id as a String or a Number");
      }
      Slice user_id_str = user_id.type() == JsonValue::Type::String ? user_id.get_string() : user_id.get_number();
      auto r_user_id = to_integer_safe<int32>(user_id_str);
      if (r_user_id.is_error()) {
        return Status::Error(400, PSLICE() << "Failed to get user_id from " << user_id_str);
      }
      if (r_user_id.ok() <= 0) {
        return Status::Error(400, PSLICE() << "Receive wrong user_id " << user_id_str);
      }
      return static_cast<int64>(r_user_id.ok());
    }
  }

  return static_cast<int64>(0);
}

Result<string> NotificationManager::decrypt_push(int64 encryption_key_id, string encryption_key, string push) {
  auto r_json_value = json_decode(push);
  if (r_json_value.is_error()) {
    return Status::Error(400, "Failed to parse payload as JSON object");
  }

  auto json_value = r_json_value.move_as_ok();
  if (json_value.type() != JsonValue::Type::Object) {
    return Status::Error(400, "Expected JSON object");
  }

  for (auto &field_value : json_value.get_object()) {
    if (field_value.first == "p") {
      auto encrypted_payload = std::move(field_value.second);
      if (encrypted_payload.type() != JsonValue::Type::String) {
        return Status::Error(400, "Expected encrypted payload as a String");
      }
      Slice data = encrypted_payload.get_string();
      if (data.size() < 12) {
        return Status::Error(400, "Encrypted payload is too small");
      }
      auto r_decoded = base64url_decode(data);
      if (r_decoded.is_error()) {
        return Status::Error(400, "Failed to base64url-decode payload");
      }
      return decrypt_push_payload(encryption_key_id, std::move(encryption_key), r_decoded.move_as_ok());
    }
  }
  return Status::Error(400, "No 'p'(payload) field found in push");
}

Result<string> NotificationManager::decrypt_push_payload(int64 encryption_key_id, string encryption_key,
                                                         string payload) {
  mtproto::AuthKey auth_key(encryption_key_id, std::move(encryption_key));
  mtproto::PacketInfo packet_info;
  packet_info.version = 2;
  packet_info.type = mtproto::PacketInfo::EndToEnd;
  packet_info.is_creator = true;
  packet_info.check_mod4 = false;

  TRY_RESULT(result, mtproto::Transport::read(payload, auth_key, &packet_info));
  if (result.type() != mtproto::Transport::ReadResult::Packet) {
    return Status::Error(400, "Wrong packet type");
  }
  if (result.packet().size() < 4) {
    return Status::Error(400, "Packet is too small");
  }
  return result.packet().substr(4).str();
}

void NotificationManager::before_get_difference() {
  if (is_disabled()) {
    return;
  }
  if (running_get_difference_) {
    return;
  }

  running_get_difference_ = true;
  on_pending_notification_update_count_changed(1, 0, "before_get_difference");
}

void NotificationManager::after_get_difference() {
  if (is_disabled()) {
    return;
  }

  CHECK(running_get_difference_);
  running_get_difference_ = false;
  on_pending_notification_update_count_changed(-1, 0, "after_get_difference");
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
  if (is_disabled()) {
    return;
  }

  VLOG(notifications) << "Before get chat difference in " << group_id;
  CHECK(group_id.is_valid());
  running_get_chat_difference_.insert(group_id.get());
  on_pending_notification_update_count_changed(1, group_id.get(), "before_get_chat_difference");
}

void NotificationManager::after_get_chat_difference(NotificationGroupId group_id) {
  if (is_disabled()) {
    return;
  }

  VLOG(notifications) << "After get chat difference in " << group_id;
  CHECK(group_id.is_valid());
  auto erased_count = running_get_chat_difference_.erase(group_id.get());
  if (erased_count == 1) {
    flush_pending_notifications_timeout_.set_timeout_in(-group_id.get(), MIN_NOTIFICATION_DELAY_MS * 1e-3);
    on_pending_notification_update_count_changed(-1, group_id.get(), "after_get_chat_difference");
  }
}

void NotificationManager::after_get_chat_difference_impl(NotificationGroupId group_id) {
  if (running_get_chat_difference_.count(group_id.get()) == 1) {
    return;
  }

  VLOG(notifications) << "Flush updates after get chat difference in " << group_id;
  CHECK(group_id.is_valid());
  if (!running_get_difference_ && pending_updates_.count(group_id.get()) == 1) {
    flush_pending_updates_timeout_.cancel_timeout(group_id.get());
    flush_pending_updates(group_id.get(), "after_get_chat_difference");
  }
}

void NotificationManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (is_disabled() || max_notification_group_count_ == 0 || is_destroyed_) {
    return;
  }

  updates.push_back(get_update_active_notifications());
  if (pending_notification_update_count_ != 0) {
    updates.push_back(td_api::make_object<td_api::updateHavePendingNotifications>(true));
  }
}

void NotificationManager::flush_all_notifications() {
  flush_all_pending_notifications();
  flush_all_pending_updates(true, "flush_all_notifications");
}

void NotificationManager::destroy_all_notifications() {
  if (is_destroyed_) {
    return;
  }

  size_t cur_pos = 0;
  for (auto it = groups_.begin(); it != groups_.end() && cur_pos < max_notification_group_count_; ++it, cur_pos++) {
    auto &group_key = it->first;
    auto &group = it->second;

    if (group_key.last_notification_date == 0) {
      break;
    }

    VLOG(notifications) << "Destroy " << group_key.group_id;
    send_remove_group_update(group_key, group, vector<int32>());
  }

  flush_all_pending_updates(true, "destroy_all_notifications");
  if (pending_notification_update_count_ != 0) {
    on_pending_notification_update_count_changed(-pending_notification_update_count_, 0, "destroy_all_notifications");
  }
  is_destroyed_ = true;
}

void NotificationManager::on_pending_notification_update_count_changed(int32 diff, int32 notification_group_id,
                                                                       const char *source) {
  bool had_pending = pending_notification_update_count_ != 0;
  pending_notification_update_count_ += diff;
  CHECK(pending_notification_update_count_ >= 0);
  VLOG(notifications) << "Update pending notification count with diff " << diff << " to "
                      << pending_notification_update_count_ << " from group " << notification_group_id << " and "
                      << source;
  bool have_pending = pending_notification_update_count_ != 0;
  if (had_pending != have_pending && !is_destroyed_) {
    auto update = td_api::make_object<td_api::updateHavePendingNotifications>(have_pending);
    VLOG(notifications) << "Send " << oneline(to_string(update));
    send_closure(G()->td(), &Td::send_update, std::move(update));
  }
}

}  // namespace td
