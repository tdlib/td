//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageSender.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

namespace td {

td_api::object_ptr<td_api::MessageSender> get_message_sender_object_const(Td *td, UserId user_id, DialogId dialog_id,
                                                                          const char *source) {
  if (dialog_id.is_valid() && td->messages_manager_->have_dialog(dialog_id)) {
    return td_api::make_object<td_api::messageSenderChat>(dialog_id.get());
  }
  if (!user_id.is_valid()) {
    // can happen only if the server sends a message with wrong sender
    LOG(ERROR) << "Receive message with wrong sender " << user_id << '/' << dialog_id << " from " << source;
    user_id = td->contacts_manager_->add_service_notifications_user();
  }
  return td_api::make_object<td_api::messageSenderUser>(td->contacts_manager_->get_user_id_object(user_id, source));
}

td_api::object_ptr<td_api::MessageSender> get_message_sender_object_const(Td *td, DialogId dialog_id,
                                                                          const char *source) {
  if (dialog_id.get_type() == DialogType::User) {
    return get_message_sender_object_const(td, dialog_id.get_user_id(), DialogId(), source);
  }
  return get_message_sender_object_const(td, UserId(), dialog_id, source);
}

td_api::object_ptr<td_api::MessageSender> get_message_sender_object(Td *td, UserId user_id, DialogId dialog_id,
                                                                    const char *source) {
  if (dialog_id.is_valid() && !td->messages_manager_->have_dialog(dialog_id)) {
    LOG(ERROR) << "Failed to find " << dialog_id;
    td->messages_manager_->force_create_dialog(dialog_id, source);
  }
  if (!user_id.is_valid() && td->auth_manager_->is_bot()) {
    td->contacts_manager_->add_anonymous_bot_user();
    td->contacts_manager_->add_service_notifications_user();
  }
  return get_message_sender_object_const(td, user_id, dialog_id, source);
}

td_api::object_ptr<td_api::MessageSender> get_message_sender_object(Td *td, DialogId dialog_id, const char *source) {
  if (dialog_id.get_type() == DialogType::User) {
    return get_message_sender_object(td, dialog_id.get_user_id(), DialogId(), source);
  }
  return get_message_sender_object(td, UserId(), dialog_id, source);
}

Result<DialogId> get_message_sender_dialog_id(const td_api::object_ptr<td_api::MessageSender> &message_sender_id) {
  if (message_sender_id == nullptr) {
    return Status::Error(400, "Member identifier is not specified");
  }
  switch (message_sender_id->get_id()) {
    case td_api::messageSenderUser::ID: {
      auto user_id = UserId(static_cast<const td_api::messageSenderUser *>(message_sender_id.get())->user_id_);
      if (!user_id.is_valid()) {
        return Status::Error(400, "Invalid user identifier specified");
      }
      return DialogId(user_id);
    }
    case td_api::messageSenderChat::ID: {
      auto dialog_id = DialogId(static_cast<const td_api::messageSenderChat *>(message_sender_id.get())->chat_id_);
      if (!dialog_id.is_valid()) {
        return Status::Error(400, "Invalid chat identifier specified");
      }
      return dialog_id;
    }
    default:
      UNREACHABLE();
      return DialogId();
  }
}

}  // namespace td
