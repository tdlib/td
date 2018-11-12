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

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"

namespace td {

extern int VERBOSITY_NAME(notifications);

class Td;

class NotificationManager : public Actor {
 public:
  NotificationManager(Td *td, ActorShared<> parent);

  NotificationId get_next_notification_id();

  NotificationGroupId get_next_notification_group_id();

  void add_notification(NotificationGroupId group_id, DialogId dialog_id, DialogId notification_settings_dialog_id,
                        bool silent, NotificationId notification_id, unique_ptr<NotificationType> type);

  void edit_notification(NotificationId notification_id, unique_ptr<NotificationType> type);

  void delete_notification(NotificationId notification_id);

  void remove_notification(NotificationId notification_id, Promise<Unit> &&promise);

  void remove_notification_group(NotificationGroupId group_id, NotificationId max_notification_id,
                                 Promise<Unit> &&promise);

 private:
  struct Notification {
    NotificationId notification_id;
    unique_ptr<NotificationType> type;
  };

  struct PendingNotification {
    DialogId settings_dialog_id;
    bool silent = false;
    NotificationId notification_id;
    unique_ptr<NotificationType> type;
  };

  struct NotificationGroup {
    DialogId dialog_id;
    int32 total_count = 0;

    vector<Notification> notifications;
    vector<PendingNotification> pending_notifications;
  };

  bool is_disabled() const;

  void start_up() override;
  void tear_down() override;

  NotificationId current_notification_id_;
  NotificationGroupId current_notification_group_id_;

  static constexpr int32 DEFAULT_NOTIFICATION_GROUP_COUNT_MAX = 10;
  static constexpr int32 DEFAULT_NOTIFICATION_GROUP_SIZE_MAX = 10;

  int32 max_notification_group_count_;
  int32 max_notification_group_size_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
