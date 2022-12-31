//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BotCommandScope.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

namespace td {

BotCommandScope::BotCommandScope(Type type, DialogId dialog_id, UserId user_id)
    : type_(type), dialog_id_(dialog_id), user_id_(user_id) {
}

Result<BotCommandScope> BotCommandScope::get_bot_command_scope(Td *td,
                                                               td_api::object_ptr<td_api::BotCommandScope> scope_ptr) {
  if (scope_ptr == nullptr) {
    return BotCommandScope(Type::Default);
  }

  CHECK(td->auth_manager_->is_bot());
  Type type;
  DialogId dialog_id;
  UserId user_id;
  switch (scope_ptr->get_id()) {
    case td_api::botCommandScopeDefault::ID:
      return BotCommandScope(Type::Default);
    case td_api::botCommandScopeAllPrivateChats::ID:
      return BotCommandScope(Type::AllUsers);
    case td_api::botCommandScopeAllGroupChats::ID:
      return BotCommandScope(Type::AllChats);
    case td_api::botCommandScopeAllChatAdministrators::ID:
      return BotCommandScope(Type::AllChatAdministrators);
    case td_api::botCommandScopeChat::ID: {
      auto scope = td_api::move_object_as<td_api::botCommandScopeChat>(scope_ptr);
      type = Type::Dialog;
      dialog_id = DialogId(scope->chat_id_);
      break;
    }
    case td_api::botCommandScopeChatAdministrators::ID: {
      auto scope = td_api::move_object_as<td_api::botCommandScopeChatAdministrators>(scope_ptr);
      type = Type::DialogAdministrators;
      dialog_id = DialogId(scope->chat_id_);
      break;
    }
    case td_api::botCommandScopeChatMember::ID: {
      auto scope = td_api::move_object_as<td_api::botCommandScopeChatMember>(scope_ptr);
      type = Type::DialogParticipant;
      dialog_id = DialogId(scope->chat_id_);
      user_id = UserId(scope->user_id_);
      TRY_STATUS(td->contacts_manager_->get_input_user(user_id));
      break;
    }
    default:
      UNREACHABLE();
      return BotCommandScope(Type::Default);
  }

  if (!td->messages_manager_->have_dialog_force(dialog_id, "get_bot_command_scope")) {
    return Status::Error(400, "Chat not found");
  }
  if (!td->messages_manager_->have_input_peer(dialog_id, AccessRights::Read)) {
    return Status::Error(400, "Can't access the chat");
  }
  switch (dialog_id.get_type()) {
    case DialogType::User:
      if (type != Type::Dialog) {
        return Status::Error(400, "Can't use specified scope in private chats");
      }
      break;
    case DialogType::Chat:
      // ok
      break;
    case DialogType::Channel:
      if (td->contacts_manager_->is_broadcast_channel(dialog_id.get_channel_id())) {
        return Status::Error(400, "Can't change commands in channel chats");
      }
      break;
    case DialogType::SecretChat:
    default:
      return Status::Error(400, "Can't access the chat");
  }

  return BotCommandScope(type, dialog_id, user_id);
}

telegram_api::object_ptr<telegram_api::BotCommandScope> BotCommandScope::get_input_bot_command_scope(
    const Td *td) const {
  auto input_peer =
      dialog_id_.is_valid() ? td->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read) : nullptr;
  auto r_input_user = td->contacts_manager_->get_input_user(user_id_);
  auto input_user = r_input_user.is_ok() ? r_input_user.move_as_ok() : nullptr;
  switch (type_) {
    case Type::Default:
      return telegram_api::make_object<telegram_api::botCommandScopeDefault>();
    case Type::AllUsers:
      return telegram_api::make_object<telegram_api::botCommandScopeUsers>();
    case Type::AllChats:
      return telegram_api::make_object<telegram_api::botCommandScopeChats>();
    case Type::AllChatAdministrators:
      return telegram_api::make_object<telegram_api::botCommandScopeChatAdmins>();
    case Type::Dialog:
      CHECK(input_peer != nullptr);
      return telegram_api::make_object<telegram_api::botCommandScopePeer>(std::move(input_peer));
    case Type::DialogAdministrators:
      CHECK(input_peer != nullptr);
      return telegram_api::make_object<telegram_api::botCommandScopePeerAdmins>(std::move(input_peer));
    case Type::DialogParticipant:
      CHECK(input_peer != nullptr);
      CHECK(input_user != nullptr);
      return telegram_api::make_object<telegram_api::botCommandScopePeerUser>(std::move(input_peer),
                                                                              std::move(input_user));
    default:
      UNREACHABLE();
      return nullptr;
  }
}

}  // namespace td
