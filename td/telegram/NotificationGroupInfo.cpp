//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationGroupInfo.h"

namespace td {

StringBuilder &operator<<(StringBuilder &string_builder, const NotificationGroupInfo &group_info) {
  return string_builder << group_info.group_id << " with last " << group_info.last_notification_id << " sent at "
                        << group_info.last_notification_date << ", max removed "
                        << group_info.max_removed_notification_id << '/' << group_info.max_removed_message_id;
}

}  // namespace td
