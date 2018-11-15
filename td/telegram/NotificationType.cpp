//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationType.h"

#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

namespace td {

class NotificationTypeMessage : public NotificationType {
  bool can_be_delayed() const override {
    return true;
  }

  td_api::object_ptr<td_api::NotificationType> get_notification_type_object(DialogId dialog_id) const override {
    auto message_object = G()->td().get_actor_unsafe()->messages_manager_->get_message_object({dialog_id, message_id_});
    if (message_object == nullptr) {
      return nullptr;
    }
    return td_api::make_object<td_api::notificationTypeNewMessage>(std::move(message_object));
  }

  StringBuilder &to_string_builder(StringBuilder &string_builder) const override {
    return string_builder << "NewMessageNotification[" << message_id_ << ']';
  }

  Type get_type() const {
    return Type::Message;
  }

  MessageId message_id_;

 public:
  explicit NotificationTypeMessage(MessageId message_id) : message_id_(message_id) {
  }
};

class NotificationTypeSecretChat : public NotificationType {
  bool can_be_delayed() const override {
    return false;
  }

  td_api::object_ptr<td_api::NotificationType> get_notification_type_object(DialogId dialog_id) const override {
    return td_api::make_object<td_api::notificationTypeNewSecretChat>();
  }

  StringBuilder &to_string_builder(StringBuilder &string_builder) const override {
    return string_builder << "NewSecretChatNotification[]";
  }

  Type get_type() const {
    return Type::SecretChat;
  }

 public:
  NotificationTypeSecretChat() {
  }
};

class NotificationTypeCall : public NotificationType {
  bool can_be_delayed() const override {
    return false;
  }

  td_api::object_ptr<td_api::NotificationType> get_notification_type_object(DialogId dialog_id) const override {
    return td_api::make_object<td_api::notificationTypeNewCall>(call_id_.get());
  }

  StringBuilder &to_string_builder(StringBuilder &string_builder) const override {
    return string_builder << "NewCallNotification[" << call_id_ << ']';
  }

  Type get_type() const {
    return Type::Call;
  }

  CallId call_id_;

 public:
  explicit NotificationTypeCall(CallId call_id) : call_id_(call_id) {
  }
};

unique_ptr<NotificationType> create_new_message_notification(MessageId message_id) {
  return make_unique<NotificationTypeMessage>(message_id);
}

unique_ptr<NotificationType> create_new_secret_chat_notification() {
  return make_unique<NotificationTypeSecretChat>();
}

unique_ptr<NotificationType> create_new_call_notification(CallId call_id) {
  return make_unique<NotificationTypeCall>(call_id);
}

}  // namespace td
