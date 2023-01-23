//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/RequestedDialogType.h"

#include "td/telegram/ChannelType.h"

namespace td {

RequestedDialogType::RequestedDialogType(td_api::object_ptr<td_api::keyboardButtonTypeRequestUser> &&request_user) {
  CHECK(request_user != nullptr);
  type_ = Type::User;
  button_id_ = request_user->id_;
  restrict_is_bot_ = request_user->restrict_user_is_bot_;
  is_bot_ = request_user->user_is_bot_;
  restrict_is_premium_ = request_user->restrict_user_is_premium_;
  is_premium_ = request_user->user_is_premium_;
}

RequestedDialogType::RequestedDialogType(td_api::object_ptr<td_api::keyboardButtonTypeRequestChat> &&request_dialog) {
  CHECK(request_dialog != nullptr);
  type_ = request_dialog->chat_is_channel_ ? Type::Channel : Type::Group;
  button_id_ = request_dialog->id_;
  restrict_is_forum_ = request_dialog->restrict_chat_is_forum_;
  is_forum_ = request_dialog->chat_is_forum_;
  bot_is_participant_ = request_dialog->bot_is_member_;
  restrict_has_username_ = request_dialog->restrict_chat_has_username_;
  has_username_ = request_dialog->chat_has_username_;
  is_created_ = request_dialog->chat_is_created_;
  restrict_user_administrator_rights_ = request_dialog->user_administrator_rights_ != nullptr;
  restrict_bot_administrator_rights_ = request_dialog->bot_administrator_rights_ != nullptr;
  auto channel_type = request_dialog->chat_is_channel_ ? ChannelType::Broadcast : ChannelType::Megagroup;
  user_administrator_rights_ = AdministratorRights(request_dialog->user_administrator_rights_, channel_type);
  bot_administrator_rights_ = AdministratorRights(request_dialog->bot_administrator_rights_, channel_type);
}

RequestedDialogType::RequestedDialogType(telegram_api::object_ptr<telegram_api::RequestPeerType> &&peer_type,
                                         int32 button_id) {
  CHECK(peer_type != nullptr);
  button_id_ = button_id;
  switch (peer_type->get_id()) {
    case telegram_api::requestPeerTypeUser::ID: {
      auto type = telegram_api::move_object_as<telegram_api::requestPeerTypeUser>(peer_type);
      type_ = Type::User;
      restrict_is_bot_ = (type->flags_ & telegram_api::requestPeerTypeUser::BOT_MASK) != 0;
      is_bot_ = type->bot_;
      restrict_is_premium_ = (type->flags_ & telegram_api::requestPeerTypeUser::PREMIUM_MASK) != 0;
      is_premium_ = type->premium_;
      break;
    }
    case telegram_api::requestPeerTypeChat::ID: {
      auto type = telegram_api::move_object_as<telegram_api::requestPeerTypeChat>(peer_type);
      type_ = Type::Group;
      restrict_is_forum_ = (type->flags_ & telegram_api::requestPeerTypeChat::FORUM_MASK) != 0;
      is_forum_ = type->forum_;
      bot_is_participant_ = type->bot_participant_;
      restrict_has_username_ = (type->flags_ & telegram_api::requestPeerTypeChat::HAS_USERNAME_MASK) != 0;
      has_username_ = type->has_username_;
      is_created_ = type->creator_;
      restrict_user_administrator_rights_ = type->user_admin_rights_ != nullptr;
      restrict_bot_administrator_rights_ = type->bot_admin_rights_ != nullptr;
      user_administrator_rights_ = AdministratorRights(type->user_admin_rights_, ChannelType::Megagroup);
      bot_administrator_rights_ = AdministratorRights(type->bot_admin_rights_, ChannelType::Megagroup);
      break;
    }
    case telegram_api::requestPeerTypeBroadcast::ID: {
      auto type = telegram_api::move_object_as<telegram_api::requestPeerTypeBroadcast>(peer_type);
      type_ = Type::Channel;
      restrict_has_username_ = (type->flags_ & telegram_api::requestPeerTypeBroadcast::HAS_USERNAME_MASK) != 0;
      has_username_ = type->has_username_;
      is_created_ = type->creator_;
      restrict_user_administrator_rights_ = type->user_admin_rights_ != nullptr;
      restrict_bot_administrator_rights_ = type->bot_admin_rights_ != nullptr;
      user_administrator_rights_ = AdministratorRights(type->user_admin_rights_, ChannelType::Broadcast);
      bot_administrator_rights_ = AdministratorRights(type->bot_admin_rights_, ChannelType::Broadcast);
      break;
    }
    default:
      UNREACHABLE();
  }
}

td_api::object_ptr<td_api::KeyboardButtonType> RequestedDialogType::get_keyboard_button_type_object() const {
  if (type_ == Type::User) {
    return td_api::make_object<td_api::keyboardButtonTypeRequestUser>(button_id_, restrict_is_bot_, is_bot_,
                                                                      restrict_is_premium_, is_premium_);
  } else {
    auto user_administrator_rights = restrict_user_administrator_rights_
                                         ? user_administrator_rights_.get_chat_administrator_rights_object()
                                         : nullptr;
    auto bot_administrator_rights =
        restrict_bot_administrator_rights_ ? bot_administrator_rights_.get_chat_administrator_rights_object() : nullptr;
    return td_api::make_object<td_api::keyboardButtonTypeRequestChat>(
        button_id_, type_ == Type::Channel, restrict_is_forum_, is_forum_, restrict_has_username_, has_username_,
        is_created_, std::move(user_administrator_rights), std::move(bot_administrator_rights), bot_is_participant_);
  }
}

telegram_api::object_ptr<telegram_api::RequestPeerType> RequestedDialogType::get_input_request_peer_type_object()
    const {
  switch (type_) {
    case Type::User: {
      int32 flags = 0;
      if (restrict_is_bot_) {
        flags |= telegram_api::requestPeerTypeUser::BOT_MASK;
      }
      if (restrict_is_premium_) {
        flags |= telegram_api::requestPeerTypeUser::PREMIUM_MASK;
      }
      return telegram_api::make_object<telegram_api::requestPeerTypeUser>(flags, is_bot_, is_premium_);
    }
    case Type::Group: {
      int32 flags = 0;
      if (restrict_is_forum_) {
        flags |= telegram_api::requestPeerTypeChat::FORUM_MASK;
      }
      if (bot_is_participant_) {
        flags |= telegram_api::requestPeerTypeChat::BOT_PARTICIPANT_MASK;
      }
      if (restrict_has_username_) {
        flags |= telegram_api::requestPeerTypeChat::HAS_USERNAME_MASK;
      }
      if (is_created_) {
        flags |= telegram_api::requestPeerTypeChat::CREATOR_MASK;
      }
      if (restrict_user_administrator_rights_) {
        flags |= telegram_api::requestPeerTypeChat::USER_ADMIN_RIGHTS_MASK;
      }
      if (restrict_bot_administrator_rights_) {
        flags |= telegram_api::requestPeerTypeChat::BOT_ADMIN_RIGHTS_MASK;
      }
      auto user_admin_rights =
          restrict_user_administrator_rights_ ? user_administrator_rights_.get_chat_admin_rights() : nullptr;
      auto bot_admin_rights =
          restrict_bot_administrator_rights_ ? bot_administrator_rights_.get_chat_admin_rights() : nullptr;
      return telegram_api::make_object<telegram_api::requestPeerTypeChat>(
          flags, false /*ignored*/, false /*ignored*/, has_username_, is_forum_, std::move(user_admin_rights),
          std::move(bot_admin_rights));
    }
    case Type::Channel: {
      int32 flags = 0;
      if (restrict_has_username_) {
        flags |= telegram_api::requestPeerTypeBroadcast::HAS_USERNAME_MASK;
      }
      if (is_created_) {
        flags |= telegram_api::requestPeerTypeBroadcast::CREATOR_MASK;
      }
      if (restrict_user_administrator_rights_) {
        flags |= telegram_api::requestPeerTypeBroadcast::USER_ADMIN_RIGHTS_MASK;
      }
      if (restrict_bot_administrator_rights_) {
        flags |= telegram_api::requestPeerTypeBroadcast::BOT_ADMIN_RIGHTS_MASK;
      }
      auto user_admin_rights =
          restrict_user_administrator_rights_ ? user_administrator_rights_.get_chat_admin_rights() : nullptr;
      auto bot_admin_rights =
          restrict_bot_administrator_rights_ ? bot_administrator_rights_.get_chat_admin_rights() : nullptr;
      return telegram_api::make_object<telegram_api::requestPeerTypeBroadcast>(
          flags, false /*ignored*/, has_username_, std::move(user_admin_rights), std::move(bot_admin_rights));
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

int32 RequestedDialogType::get_button_id() const {
  return button_id_;
}

}  // namespace td
