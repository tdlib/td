//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/NotificationGroupId.h"
#include "td/telegram/NotificationGroupKey.h"
#include "td/telegram/NotificationId.h"

namespace td {

class NotificationGroupInfo {
  bool try_reuse_ = false;  // true, if the group needs to be deleted from database and tried to be reused

 public:
  NotificationGroupId group_id;
  int32 last_notification_date = 0;            // date of last notification in the group
  NotificationId last_notification_id;         // identifier of last notification in the group
  NotificationId max_removed_notification_id;  // notification identifier, up to which all notifications are removed
  MessageId max_removed_message_id;            // message identifier, up to which all notifications are removed
  bool is_changed = false;                     // true, if the group needs to be saved to database

  bool is_active() const {
    return group_id.is_valid() && !try_reuse_;
  }

  void try_reuse();

  void add_group_key_if_changed(vector<NotificationGroupKey> &group_keys, DialogId dialog_id);

  NotificationGroupId get_reused_group_id();

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

StringBuilder &operator<<(StringBuilder &string_builder, const NotificationGroupInfo &group_info);

}  // namespace td
