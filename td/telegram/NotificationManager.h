//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/Notification.h"
#include "td/telegram/NotificationGroupId.h"
#include "td/telegram/NotificationGroupKey.h"
#include "td/telegram/NotificationId.h"
#include "td/telegram/NotificationType.h"
#include "td/telegram/td_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

#include <functional>
#include <map>
#include <unordered_map>

namespace td {

extern int VERBOSITY_NAME(notifications);

class Td;

class NotificationManager : public Actor {
 public:
  static constexpr int32 MIN_NOTIFICATION_GROUP_COUNT_MAX = 1;
  static constexpr int32 MAX_NOTIFICATION_GROUP_COUNT_MAX = 25;
  static constexpr int32 MIN_NOTIFICATION_GROUP_SIZE_MAX = 1;
  static constexpr int32 MAX_NOTIFICATION_GROUP_SIZE_MAX = 25;

  NotificationManager(Td *td, ActorShared<> parent);

  size_t get_max_notification_group_size() const;

  NotificationId get_max_notification_id() const;

  NotificationId get_next_notification_id();

  NotificationGroupId get_next_notification_group_id();

  void load_group_force(NotificationGroupId group_id);

  void add_notification(NotificationGroupId group_id, DialogId dialog_id, int32 date,
                        DialogId notification_settings_dialog_id, bool is_silent, NotificationId notification_id,
                        unique_ptr<NotificationType> type);

  void edit_notification(NotificationGroupId group_id, NotificationId notification_id,
                         unique_ptr<NotificationType> type);

  void remove_notification(NotificationGroupId group_id, NotificationId notification_id, bool is_permanent,
                           Promise<Unit> &&promise);

  void remove_notification_group(NotificationGroupId group_id, NotificationId max_notification_id,
                                 MessageId max_message_id, int32 new_total_count, Promise<Unit> &&promise);

  void on_notification_group_count_max_changed();

  void on_notification_group_size_max_changed();

  void on_online_cloud_timeout_changed();

  void on_notification_cloud_delay_changed();

  void on_notification_default_delay_changed();

  void before_get_difference();

  void after_get_difference();

  void before_get_chat_difference(NotificationGroupId group_id);

  void after_get_chat_difference(NotificationGroupId group_id);

 private:
  static constexpr int32 DEFAULT_GROUP_COUNT_MAX = 10;
  static constexpr int32 DEFAULT_GROUP_SIZE_MAX = 10;
  static constexpr size_t EXTRA_GROUP_SIZE = 10;

  static constexpr int32 DEFAULT_ONLINE_CLOUD_TIMEOUT_MS = 300000;
  static constexpr int32 DEFAULT_ONLINE_CLOUD_DELAY_MS = 30000;
  static constexpr int32 DEFAULT_DEFAULT_DELAY_MS = 1500;

  static constexpr int32 MIN_NOTIFICATION_DELAY_MS = 1;

  static constexpr int32 MIN_UPDATE_DELAY_MS = 50;
  static constexpr int32 MAX_UPDATE_DELAY_MS = 60000;

  struct PendingNotification {
    int32 date = 0;
    DialogId settings_dialog_id;
    bool is_silent = false;
    NotificationId notification_id;
    unique_ptr<NotificationType> type;
  };

  struct NotificationGroup {
    int32 total_count = 0;

    vector<Notification> notifications;

    double pending_notifications_flush_time = 0;
    vector<PendingNotification> pending_notifications;
  };

  using NotificationGroups = std::map<NotificationGroupKey, NotificationGroup>;

  static void on_flush_pending_notifications_timeout_callback(void *notification_manager_ptr, int64 group_id_int);

  static void on_flush_pending_updates_timeout_callback(void *notification_manager_ptr, int64 group_id_int);

  bool is_disabled() const;

  void start_up() override;
  void tear_down() override;

  void add_update(int32 group_id, td_api::object_ptr<td_api::Update> update);

  void add_update_notification_group(td_api::object_ptr<td_api::updateNotificationGroup> update);

  void add_update_notification(NotificationGroupId notification_group_id, DialogId dialog_id,
                               const Notification &notification);

  NotificationGroups::iterator add_group(NotificationGroupKey &&group_key, NotificationGroup &&group);

  NotificationGroups::iterator get_group(NotificationGroupId group_id);

  NotificationGroups::iterator get_group_force(NotificationGroupId group_id, bool send_update = true);

  void delete_group(NotificationGroups::iterator &&group_it);

  int32 load_message_notification_groups_from_database(int32 limit, bool send_update);

  NotificationGroupKey get_last_updated_group_key() const;

  void send_remove_group_update(const NotificationGroupKey &group_key, const NotificationGroup &group,
                                vector<int32> &&removed_notification_ids);

  void send_add_group_update(const NotificationGroupKey &group_key, const NotificationGroup &group);

  int32 get_notification_delay_ms(DialogId dialog_id, const PendingNotification &notification) const;

  void do_flush_pending_notifications(NotificationGroupKey &group_key, NotificationGroup &group,
                                      vector<PendingNotification> &pending_notifications);

  void flush_pending_notifications(NotificationGroupId group_id);

  void flush_all_pending_notifications();

  void on_notifications_removed(NotificationGroups::iterator &&group_it,
                                vector<td_api::object_ptr<td_api::notification>> &&added_notifications,
                                vector<int32> &&removed_notification_ids);

  void remove_added_notifications_from_pending_updates(
      NotificationGroupId group_id,
      std::function<bool(const td_api::object_ptr<td_api::notification> &notification)> is_removed);

  void flush_pending_updates(int32 group_id, const char *source);

  void flush_all_pending_updates(bool include_delayed_chats, const char *source);

  NotificationId current_notification_id_;
  NotificationGroupId current_notification_group_id_;

  size_t max_notification_group_count_ = 0;
  size_t max_notification_group_size_ = 0;
  size_t keep_notification_group_size_ = 0;

  int32 online_cloud_timeout_ms_ = DEFAULT_ONLINE_CLOUD_TIMEOUT_MS;
  int32 notification_cloud_delay_ms_ = DEFAULT_ONLINE_CLOUD_DELAY_MS;
  int32 notification_default_delay_ms_ = DEFAULT_DEFAULT_DELAY_MS;

  NotificationGroupKey last_loaded_notification_group_key_;

  bool running_get_difference_ = false;
  std::unordered_set<int32> running_get_chat_difference_;

  NotificationGroups groups_;
  std::unordered_map<NotificationGroupId, NotificationGroupKey, NotificationGroupIdHash> group_keys_;

  std::unordered_map<int32, vector<td_api::object_ptr<td_api::Update>>> pending_updates_;

  void after_get_difference_impl();

  void after_get_chat_difference_impl(NotificationGroupId group_id);

  MultiTimeout flush_pending_notifications_timeout_{"FlushPendingNotificationsTimeout"};
  MultiTimeout flush_pending_updates_timeout_{"FlushPendingUpdatesTimeout"};

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
