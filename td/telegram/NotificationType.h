//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CallId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class NotificationType {
 public:
  NotificationType() = default;
  NotificationType(const NotificationType &) = delete;
  NotificationType &operator=(const NotificationType &) = delete;
  NotificationType(NotificationType &&) = delete;
  NotificationType &operator=(NotificationType &&) = delete;

  virtual ~NotificationType() {
  }

  virtual bool can_be_delayed() const = 0;

  virtual MessageId get_message_id() const = 0;

  virtual td_api::object_ptr<td_api::NotificationType> get_notification_type_object(DialogId dialog_id) const = 0;

  virtual StringBuilder &to_string_builder(StringBuilder &string_builder) const = 0;

 protected:
  // append only
  enum class Type : int32 { Message, SecretChat, Call };

  virtual Type get_type() const = 0;
};

inline StringBuilder &operator<<(StringBuilder &string_builder, const NotificationType &notification_type) {
  return notification_type.to_string_builder(string_builder);
}

unique_ptr<NotificationType> create_new_message_notification(MessageId message_id);

unique_ptr<NotificationType> create_new_secret_chat_notification();

unique_ptr<NotificationType> create_new_call_notification(CallId call_id);

}  // namespace td
