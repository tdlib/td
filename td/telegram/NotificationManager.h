//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/PromiseFuture.h"

namespace td {

class Td;

class NotificationManager {
 public:
  explicit NotificationManager(Td *td);

  void remove_notification(int32 notification_id, Promise<Unit> &&promise);

  void remove_notifications(int32 group_id, int32 max_notification_id, Promise<Unit> &&promise);

 private:
  static bool is_valid_notification_id(int32 notification_id);

  static bool is_valid_group_id(int32 group_id);

  Td *td_;
};

}  // namespace td
