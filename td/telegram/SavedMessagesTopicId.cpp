//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SavedMessagesTopicId.h"

#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/MessageForwardInfo.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

namespace td {

static constexpr DialogId HIDDEN_AUTHOR_DIALOG_ID = DialogId(static_cast<int64>(2666000));

SavedMessagesTopicId::SavedMessagesTopicId(DialogId my_dialog_id, const MessageForwardInfo *message_forward_info) {
  if (message_forward_info != nullptr) {
    auto last_dialog_id = message_forward_info->get_last_dialog_id();
    if (last_dialog_id.is_valid()) {
      dialog_id_ = last_dialog_id;
      return;
    }
    auto from_dialog_id = message_forward_info->get_origin().get_sender();
    if (from_dialog_id.is_valid()) {
      dialog_id_ = my_dialog_id;
      return;
    }
    if (message_forward_info->get_origin().is_sender_hidden()) {
      dialog_id_ = HIDDEN_AUTHOR_DIALOG_ID;
    }
  }
  dialog_id_ = my_dialog_id;
}

td_api::object_ptr<td_api::SavedMessagesTopic> SavedMessagesTopicId::get_saved_messages_topic_object(Td *td) const {
  if (dialog_id_ == DialogId()) {
    return nullptr;
  }
  if (dialog_id_ == td->dialog_manager_->get_my_dialog_id()) {
    return td_api::make_object<td_api::savedMessagesTopicMyNotes>();
  }
  if (dialog_id_ == HIDDEN_AUTHOR_DIALOG_ID) {
    return td_api::make_object<td_api::savedMessagesTopicAuthorHidden>();
  }
  return td_api::make_object<td_api::savedMessagesTopicSavedFromChat>(
      td->messages_manager_->get_chat_id_object(dialog_id_, "savedMessagesTopicSavedFromChat"));
}

void SavedMessagesTopicId::add_dependencies(Dependencies &dependencies) const {
  if (dialog_id_ == HIDDEN_AUTHOR_DIALOG_ID) {
    dependencies.add_dialog_dependencies(dialog_id_);
  } else {
    dependencies.add_dialog_and_dependencies(dialog_id_);
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, SavedMessagesTopicId saved_messages_topic_id) {
  if (!saved_messages_topic_id.dialog_id_.is_valid()) {
    return string_builder << "[no Saved Messages topic]";
  }
  if (saved_messages_topic_id.dialog_id_ == HIDDEN_AUTHOR_DIALOG_ID) {
    return string_builder << "[Author Hidden topic]";
  }
  return string_builder << "[topic of" << saved_messages_topic_id.dialog_id_ << ']';
}

}  // namespace td
