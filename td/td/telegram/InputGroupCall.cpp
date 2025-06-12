//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/InputGroupCall.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

namespace td {

Result<InputGroupCall> InputGroupCall::get_input_group_call(
    Td *td, td_api::object_ptr<td_api::InputGroupCall> &&input_group_call) {
  if (input_group_call == nullptr) {
    return Status::Error(400, "Input group call must be non-empty");
  }
  InputGroupCall result;
  switch (input_group_call->get_id()) {
    case td_api::inputGroupCallLink::ID: {
      auto link = td_api::move_object_as<td_api::inputGroupCallLink>(input_group_call);
      result.slug_ = LinkManager::get_group_call_invite_link_slug(link->link_);
      if (result.slug_.empty()) {
        return Status::Error(400, "Invalid group call invite link specified");
      }
      break;
    }
    case td_api::inputGroupCallMessage::ID: {
      auto link = td_api::move_object_as<td_api::inputGroupCallMessage>(input_group_call);
      TRY_RESULT(server_message_id, td->messages_manager_->get_group_call_message_id(
                                        {DialogId(link->chat_id_), MessageId(link->message_id_)}));
      result.server_message_id_ = server_message_id;
      break;
    }
    default:
      UNREACHABLE();
  }
  return result;
}

telegram_api::object_ptr<telegram_api::InputGroupCall> InputGroupCall::get_input_group_call() const {
  if (!slug_.empty()) {
    return telegram_api::make_object<telegram_api::inputGroupCallSlug>(slug_);
  }
  if (server_message_id_.is_valid()) {
    return telegram_api::make_object<telegram_api::inputGroupCallInviteMessage>(server_message_id_.get());
  }
  UNREACHABLE();
  return nullptr;
}

StringBuilder &operator<<(StringBuilder &string_builder, InputGroupCall input_group_call) {
  if (!input_group_call.slug_.empty()) {
    return string_builder << "group call " << input_group_call.slug_;
  }
  if (input_group_call.server_message_id_.is_valid()) {
    return string_builder << "group call " << input_group_call.server_message_id_.get();
  }
  UNREACHABLE();
  return string_builder;
}

}  // namespace td
