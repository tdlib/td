//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

enum class NotificationGroupType : int8 { Messages, Mentions, SecretChat, Calls };

inline td_api::object_ptr<td_api::NotificationGroupType> get_notification_group_type_object(
    NotificationGroupType type) {
  switch (type) {
    case NotificationGroupType::Messages:
      return td_api::make_object<td_api::notificationGroupTypeMessages>();
    case NotificationGroupType::Mentions:
      return td_api::make_object<td_api::notificationGroupTypeMentions>();
    case NotificationGroupType::SecretChat:
      return td_api::make_object<td_api::notificationGroupTypeSecretChat>();
    case NotificationGroupType::Calls:
      return td_api::make_object<td_api::notificationGroupTypeCalls>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

inline NotificationGroupType get_notification_group_type(
    const td_api::object_ptr<td_api::NotificationGroupType> &type) {
  CHECK(type != nullptr);
  switch (type->get_id()) {
    case td_api::notificationGroupTypeMessages::ID:
      return NotificationGroupType::Messages;
    case td_api::notificationGroupTypeMentions::ID:
      return NotificationGroupType::Mentions;
    case td_api::notificationGroupTypeSecretChat::ID:
      return NotificationGroupType::SecretChat;
    case td_api::notificationGroupTypeCalls::ID:
      return NotificationGroupType::Calls;
    default:
      UNREACHABLE();
      return NotificationGroupType::Calls;
  }
}

inline StringBuilder &operator<<(StringBuilder &sb, const NotificationGroupType &type) {
  switch (type) {
    case NotificationGroupType::Messages:
      return sb << "Messages";
    case NotificationGroupType::Mentions:
      return sb << "Mentions";
    case NotificationGroupType::SecretChat:
      return sb << "SecretChat";
    case NotificationGroupType::Calls:
      return sb << "Calls";
    default:
      UNREACHABLE();
      return sb;
  }
}

}  // namespace td
