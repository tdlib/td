//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageForwardInfo.h"

#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"

namespace td {

unique_ptr<MessageForwardInfo> MessageForwardInfo::get_message_forward_info(
    Td *td, telegram_api::object_ptr<telegram_api::messageFwdHeader> &&forward_header) {
  if (forward_header == nullptr) {
    return nullptr;
  }
  auto date = forward_header->date_;
  if (date <= 0) {
    LOG(ERROR) << "Wrong date in message forward header: " << oneline(to_string(forward_header));
    return nullptr;
  }

  DialogId from_dialog_id;
  MessageId from_message_id;
  if (forward_header->saved_from_peer_ != nullptr) {
    from_dialog_id = DialogId(forward_header->saved_from_peer_);
    from_message_id = MessageId(ServerMessageId(forward_header->saved_from_msg_id_));
    if (!from_dialog_id.is_valid() || !from_message_id.is_valid()) {
      LOG(ERROR) << "Receive " << from_message_id << " in " << from_dialog_id
                 << " in message forward header: " << oneline(to_string(forward_header));
      from_dialog_id = DialogId();
      from_message_id = MessageId();
    } else {
      td->dialog_manager_->force_create_dialog(from_dialog_id, "get_message_forward_info", true);
    }
  }
  bool is_imported = forward_header->imported_;
  auto psa_type = std::move(forward_header->psa_type_);
  auto r_origin = MessageOrigin::get_message_origin(td, std::move(forward_header));
  if (r_origin.is_error()) {
    return nullptr;
  }

  return td::make_unique<MessageForwardInfo>(r_origin.move_as_ok(), date, from_dialog_id, from_message_id,
                                             std::move(psa_type), is_imported);
}

td_api::object_ptr<td_api::messageForwardInfo> MessageForwardInfo::get_message_forward_info_object(Td *td) const {
  if (is_imported) {
    return nullptr;
  }
  return td_api::make_object<td_api::messageForwardInfo>(
      origin.get_message_origin_object(td), date, psa_type,
      td->messages_manager_->get_chat_id_object(from_dialog_id, "messageForwardInfo"), from_message_id.get());
}

td_api::object_ptr<td_api::messageImportInfo> MessageForwardInfo::get_message_import_info_object() const {
  if (!is_imported) {
    return nullptr;
  }
  return td_api::make_object<td_api::messageImportInfo>(origin.get_sender_name(), date);
}

void MessageForwardInfo::add_dependencies(Dependencies &dependencies) const {
  origin.add_dependencies(dependencies);
  dependencies.add_dialog_and_dependencies(from_dialog_id);
}

void MessageForwardInfo::add_min_user_ids(vector<UserId> &user_ids) const {
  origin.add_user_ids(user_ids);
  // from_dialog_id can be a user only in Saved Messages
}

void MessageForwardInfo::add_min_channel_ids(vector<ChannelId> &channel_ids) const {
  origin.add_channel_ids(channel_ids);
  if (from_dialog_id.get_type() == DialogType::Channel) {
    channel_ids.push_back(from_dialog_id.get_channel_id());
  }
}

bool operator==(const MessageForwardInfo &lhs, const MessageForwardInfo &rhs) {
  return lhs.origin == rhs.origin && lhs.date == rhs.date && lhs.from_dialog_id == rhs.from_dialog_id &&
         lhs.from_message_id == rhs.from_message_id && lhs.psa_type == rhs.psa_type &&
         lhs.is_imported == rhs.is_imported;
}

bool operator!=(const MessageForwardInfo &lhs, const MessageForwardInfo &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageForwardInfo &forward_info) {
  string_builder << "MessageForwardInfo[" << (forward_info.is_imported ? "imported " : "") << forward_info.origin;
  if (!forward_info.psa_type.empty()) {
    string_builder << ", psa_type " << forward_info.psa_type;
  }
  if (forward_info.from_dialog_id.is_valid() || forward_info.from_message_id.is_valid()) {
    string_builder << ", from " << MessageFullId(forward_info.from_dialog_id, forward_info.from_message_id);
  }
  return string_builder << " at " << forward_info.date << ']';
}

}  // namespace td
