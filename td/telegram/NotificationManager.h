//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/NotificationType.h"

#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"

namespace td {

class Td;

class NotificationManager {
 public:
  explicit NotificationManager(Td *td);

  int32 get_next_notification_id();

  void add_notification(int32 group_id, int32 total_count, DialogId dialog_id, DialogId notification_settings_dialog_id,
                        bool silent, int32 notification_id, unique_ptr<NotificationType> type);

  void edit_notification(int32 notification_id, unique_ptr<NotificationType> type);

  void delete_notification(int32 notification_id);

  void remove_notification(int32 notification_id, Promise<Unit> &&promise);

  void remove_notification_group(int32 group_id, int32 max_notification_id, Promise<Unit> &&promise);

 private:
  static bool is_valid_notification_id(int32 notification_id);

  static bool is_valid_group_id(int32 group_id);

  Td *td_;
};

}  // namespace td
