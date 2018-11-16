//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/NotificationGroupId.h"
#include "td/telegram/NotificationId.h"
#include "td/telegram/NotificationType.h"
#include "td/telegram/td_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

#include <map>

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

  NotificationId get_next_notification_id();

  NotificationGroupId get_next_notification_group_id();

  void add_notification(NotificationGroupId group_id, DialogId dialog_id, int32 date,
                        DialogId notification_settings_dialog_id, bool is_silent, NotificationId notification_id,
                        unique_ptr<NotificationType> type);

  void edit_notification(NotificationGroupId group_id, NotificationId notification_id,
                         unique_ptr<NotificationType> type);

  void remove_notification(NotificationGroupId group_id, NotificationId notification_id, Promise<Unit> &&promise);

  void remove_notification_group(NotificationGroupId group_id, NotificationId max_notification_id,
                                 Promise<Unit> &&promise);

  void on_notification_group_count_max_changed();

  void on_notification_group_size_max_changed();

  void on_online_cloud_timeout_changed();

  void on_notification_cloud_delay_changed();

  void on_notification_default_delay_changed();

 private:
  static constexpr int32 DEFAULT_GROUP_COUNT_MAX = 10;
  static constexpr int32 DEFAULT_GROUP_SIZE_MAX = 10;
  static constexpr size_t EXTRA_GROUP_SIZE = 10;

  static constexpr int32 DEFAULT_ONLINE_CLOUD_TIMEOUT_MS = 300000;
  static constexpr int32 DEFAULT_ONLINE_CLOUD_DELAY_MS = 30000;
  static constexpr int32 DEFAULT_DEFAULT_DELAY_MS = 1500;

  static constexpr int32 MIN_NOTIFICATION_DELAY_MS = 1;

  struct Notification {
    NotificationId notification_id;
    unique_ptr<NotificationType> type;

    Notification(NotificationId notification_id, unique_ptr<NotificationType> type)
        : notification_id(notification_id), type(std::move(type)) {
    }
  };

  struct PendingNotification {
    int32 date = 0;
    DialogId settings_dialog_id;
    bool is_silent = false;
    NotificationId notification_id;
    unique_ptr<NotificationType> type;
  };

  struct NotificationGroupKey {
    NotificationGroupId group_id;
    DialogId dialog_id;
    int32 last_notification_date = 0;

    bool operator<(const NotificationGroupKey &other) const {
      if (last_notification_date != other.last_notification_date) {
        return last_notification_date > other.last_notification_date;
      }
      if (dialog_id != other.dialog_id) {
        return dialog_id.get() > other.dialog_id.get();
      }
      return group_id.get() > other.group_id.get();
    }

    friend StringBuilder &operator<<(StringBuilder &string_builder, const NotificationGroupKey &group_key) {
      return string_builder << '[' << group_key.group_id << ',' << group_key.dialog_id << ','
                            << group_key.last_notification_date << ']';
    }
  };
  struct NotificationGroup {
    int32 total_count = 0;

    vector<Notification> notifications;

    double pending_notifications_flush_time = 0;
    vector<PendingNotification> pending_notifications;
  };

  using NotificationGroups = std::map<NotificationGroupKey, NotificationGroup>;

  static void on_flush_pending_notifications_timeout_callback(void *notification_manager_ptr, int64 group_id_int);

  bool is_disabled() const;

  void start_up() override;
  void tear_down() override;

  static td_api::object_ptr<td_api::notification> get_notification_object(DialogId dialog_id,
                                                                          const Notification &notification);

  void send_update_notification_group(td_api::object_ptr<td_api::updateNotificationGroup> update);

  void send_update_notification(NotificationGroupId notification_group_id, DialogId dialog_id,
                                const Notification &notification);

  NotificationGroups::iterator get_group(NotificationGroupId group_id);

  NotificationGroupKey get_last_updated_group_key() const;

  void send_remove_group_update(NotificationGroupId group_id);

  void send_add_group_update(const NotificationGroupKey &group_key, const NotificationGroup &group);

  int32 get_notification_delay_ms(DialogId dialog_id, const PendingNotification &notification) const;

  void flush_pending_notifications(NotificationGroupKey &group_key, NotificationGroup &group,
                                   vector<PendingNotification> &pending_notifications);

  void flush_pending_notifications(NotificationGroupId group_id);

  NotificationId current_notification_id_;
  NotificationGroupId current_notification_group_id_;

  size_t max_notification_group_count_ = 0;
  size_t max_notification_group_size_ = 0;
  size_t keep_notification_group_size_ = 0;

  int32 online_cloud_timeout_ms_ = DEFAULT_ONLINE_CLOUD_TIMEOUT_MS;
  int32 notification_cloud_delay_ms_ = DEFAULT_ONLINE_CLOUD_DELAY_MS;
  int32 notification_default_delay_ms_ = DEFAULT_DEFAULT_DELAY_MS;

  NotificationGroups groups_;

  MultiTimeout flush_pending_notifications_timeout_{"FlushPendingNotificationsTimeout"};

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
