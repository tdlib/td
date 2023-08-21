//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageId.h"
#include "td/telegram/NotificationGroupId.h"
#include "td/telegram/NotificationId.h"

namespace td {

struct NotificationGroupInfo {
  NotificationGroupId group_id;
  int32 last_notification_date = 0;            // date of last notification in the group
  NotificationId last_notification_id;         // identifier of last notification in the group
  NotificationId max_removed_notification_id;  // notification identifier, up to which all notifications are removed
  MessageId max_removed_message_id;            // message identifier, up to which all notifications are removed
  bool is_changed = false;                     // true, if the group needs to be saved to database
  bool try_reuse = false;  // true, if the group needs to be deleted from database and tried to be reused

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

StringBuilder &operator<<(StringBuilder &string_builder, const NotificationGroupInfo &group_info);

}  // namespace td
