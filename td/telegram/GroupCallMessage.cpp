//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallMessage.h"

#include "td/telegram/MessageSender.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/logging.h"

namespace td {

GroupCallMessage::GroupCallMessage(Td *td, telegram_api::object_ptr<telegram_api::groupCallMessage> &&message)
    : id_(message->id_)
    , dialog_id_(message->from_id_)
    , text_(
          get_formatted_text(td->user_manager_.get(), std::move(message->message_), true, false, "GroupCallMessage")) {
}

GroupCallMessage::GroupCallMessage(DialogId dialog_id, FormattedText text)
    : dialog_id_(dialog_id), text_(std::move(text)) {
}

td_api::object_ptr<td_api::groupCallMessage> GroupCallMessage::get_group_call_message_object(Td *td) const {
  return td_api::make_object<td_api::groupCallMessage>(
      get_message_sender_object(td, dialog_id_, "get_group_call_message_object"),
      get_formatted_text_object(td->user_manager_.get(), text_, true, -1));
}

StringBuilder &operator<<(StringBuilder &string_builder, const GroupCallMessage &group_call_message) {
  return string_builder << "GroupCallMessage[" << group_call_message.id_ << " by " << group_call_message.dialog_id_
                        << ": " << group_call_message.text_ << ']';
}

}  // namespace td
