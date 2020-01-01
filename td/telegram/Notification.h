//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/NotificationId.h"
#include "td/telegram/NotificationType.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Notification {
 public:
  NotificationId notification_id;
  int32 date = 0;
  bool is_silent = false;
  unique_ptr<NotificationType> type;

  Notification(NotificationId notification_id, int32 date, bool is_silent, unique_ptr<NotificationType> type)
      : notification_id(notification_id), date(date), is_silent(is_silent), type(std::move(type)) {
  }
};

inline td_api::object_ptr<td_api::notification> get_notification_object(DialogId dialog_id,
                                                                        const Notification &notification) {
  CHECK(notification.type != nullptr);
  return td_api::make_object<td_api::notification>(notification.notification_id.get(), notification.date,
                                                   notification.is_silent,
                                                   notification.type->get_notification_type_object(dialog_id));
}

inline StringBuilder &operator<<(StringBuilder &sb, const Notification &notification) {
  return sb << "notification[" << notification.notification_id << ", " << notification.date << ", "
            << notification.is_silent << ", " << *notification.type << ']';
}

}  // namespace td
