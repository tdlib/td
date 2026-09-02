//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/InlineKeyboardButton.h"

#include "td/telegram/Dependencies.h"
#include "td/telegram/Global.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/TargetDialogTypes.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserManager.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/SliceBuilder.h"

#include <limits>

namespace td {

InlineKeyboardButton InlineKeyboardButton::copy() const {
  InlineKeyboardButton result;
  result.type_ = type_;
  result.id_ = id_;
  result.user_id_ = user_id_;
  result.style_ = style_;
  result.text_ = text_;
  result.forward_text_ = forward_text_;
  result.data_ = data_;
  return result;
}

void InlineKeyboardButton::add_dependencies(Dependencies &dependencies) const {
  dependencies.add(user_id_);
}

InlineKeyboardButton InlineKeyboardButton::clone(DialogId dialog_id, const MessageContentDupType &dup_type,
                                                 bool is_via_bot, bool is_rich_message) const {
  if (dialog_id.get_type() == DialogType::SecretChat) {
    // secret chats have no reply markup support
    return InlineKeyboardButton();
  }
  if (dup_type == MessageContentDupType::Send || dup_type == MessageContentDupType::SendViaBot ||
      dup_type == MessageContentDupType::SendQuickReply) {
    // any button can be sent
    return copy();
  }
  bool is_forward = dup_type == MessageContentDupType::Forward;
  if (!is_forward && !is_rich_message) {
    // keyboard buttons can't be copied
    return InlineKeyboardButton();
  }
  if (type_ == Type::Url || type_ == Type::User || type_ == Type::Copy) {
    return copy();
  }
  if (type_ == Type::UrlAuth) {
    auto result = copy();
    if (is_forward) {
      if (!result.forward_text_.empty()) {
        result.text_ = std::move(result.forward_text_);
        result.forward_text_.clear();
      }
    } else {
      CHECK(is_rich_message);
      result.type_ = Type::Url;
      result.forward_text_.clear();
      result.id_ = 0;
    }
    return result;
  }
  if (is_forward && is_via_bot && (type_ == Type::SwitchInline || type_ == Type::SwitchInlineCurrentDialog)) {
    auto result = copy();
    result.type_ = Type::SwitchInline;
    return result;
  }
  return InlineKeyboardButton();
}

bool operator==(const InlineKeyboardButton &lhs, const InlineKeyboardButton &rhs) {
  return lhs.type_ == rhs.type_ && lhs.id_ == rhs.id_ && lhs.user_id_ == rhs.user_id_ && lhs.style_ == rhs.style_ &&
         lhs.text_ == rhs.text_ && lhs.forward_text_ == rhs.forward_text_ && lhs.data_ == rhs.data_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const InlineKeyboardButton &keyboard_button) {
  string_builder << "Button[";
  switch (keyboard_button.type_) {
    case InlineKeyboardButton::Type::Url:
      string_builder << "Url";
      break;
    case InlineKeyboardButton::Type::Callback:
      string_builder << "Callback";
      break;
    case InlineKeyboardButton::Type::CallbackGame:
      string_builder << "CallbackGame";
      break;
    case InlineKeyboardButton::Type::SwitchInline:
      string_builder << "SwitchInline, target chats = " << TargetDialogTypes(keyboard_button.id_);
      break;
    case InlineKeyboardButton::Type::SwitchInlineCurrentDialog:
      string_builder << "SwitchInlineCurrentChat";
      break;
    case InlineKeyboardButton::Type::Buy:
      string_builder << "Buy";
      break;
    case InlineKeyboardButton::Type::UrlAuth:
      string_builder << "UrlAuth, ID = " << keyboard_button.id_;
      if (!keyboard_button.forward_text_.empty()) {
        string_builder << ", forward text = " << keyboard_button.forward_text_;
      }
      break;
    case InlineKeyboardButton::Type::CallbackWithPassword:
      string_builder << "CallbackWithPassword";
      break;
    case InlineKeyboardButton::Type::User:
      string_builder << "User " << keyboard_button.user_id_.get();
      break;
    case InlineKeyboardButton::Type::WebView:
      string_builder << "WebView";
      break;
    case InlineKeyboardButton::Type::Copy:
      string_builder << "Copy";
      break;
    case InlineKeyboardButton::Type::Disabled:
      string_builder << "Disabled";
      break;
    default:
      UNREACHABLE();
  }
  return string_builder << ", text = " << keyboard_button.text_ << keyboard_button.style_ << ", "
                        << keyboard_button.data_ << ']';
}

InlineKeyboardButton::InlineKeyboardButton(
    telegram_api::object_ptr<telegram_api::keyboardInlineButton> &&keyboard_button) {
  CHECK(keyboard_button != nullptr);

  switch (keyboard_button->type_->get_id()) {
    case telegram_api::inlineButtonTypeUrl::ID: {
      auto type = telegram_api::move_object_as<telegram_api::inlineButtonTypeUrl>(keyboard_button->type_);
      auto r_url = LinkManager::check_link(type->url_);
      if (r_url.is_error()) {
        LOG(ERROR) << "Inline keyboard " << r_url.error().message();
        break;
      }
      type_ = Type::Url;
      data_ = r_url.move_as_ok();
      break;
    }
    case telegram_api::inlineButtonTypeCallback::ID: {
      auto type = telegram_api::move_object_as<telegram_api::inlineButtonTypeCallback>(keyboard_button->type_);
      type_ = type->requires_password_ ? Type::CallbackWithPassword : Type::Callback;
      data_ = type->data_.as_slice().str();
      break;
    }
    case telegram_api::inlineButtonTypeGame::ID:
      type_ = Type::CallbackGame;
      break;
    case telegram_api::inlineButtonTypeSwitchInline::ID: {
      auto type = telegram_api::move_object_as<telegram_api::inlineButtonTypeSwitchInline>(keyboard_button->type_);
      type_ = type->same_peer_ ? Type::SwitchInlineCurrentDialog : Type::SwitchInline;
      data_ = std::move(type->query_);
      if (!type->same_peer_) {
        id_ = TargetDialogTypes(type->peer_types_).get_mask();
      }
      break;
    }
    case telegram_api::inlineButtonTypeBuy::ID:
      type_ = Type::Buy;
      break;
    case telegram_api::inlineButtonTypeUrlAuth::ID: {
      auto type = telegram_api::move_object_as<telegram_api::inlineButtonTypeUrlAuth>(keyboard_button->type_);
      auto r_url = LinkManager::check_link(type->url_);
      if (r_url.is_error()) {
        LOG(ERROR) << "Inline keyboard Login " << r_url.error().message();
        break;
      }
      type_ = Type::UrlAuth;
      id_ = type->button_id_;
      forward_text_ = std::move(type->fwd_text_);
      data_ = r_url.move_as_ok();
      break;
    }
    case telegram_api::inlineButtonTypeUserProfile::ID: {
      auto type = telegram_api::move_object_as<telegram_api::inlineButtonTypeUserProfile>(keyboard_button->type_);
      auto user_id = UserId(type->user_id_);
      if (!user_id.is_valid()) {
        LOG(ERROR) << "Receive " << user_id << " in inline keyboard";
        break;
      }
      type_ = Type::User;
      user_id_ = user_id;
      break;
    }
    case telegram_api::inlineButtonTypeWebView::ID: {
      auto type = telegram_api::move_object_as<telegram_api::inlineButtonTypeWebView>(keyboard_button->type_);
      auto r_url = LinkManager::check_link(type->url_);
      if (r_url.is_error()) {
        LOG(ERROR) << "Inline keyboard Web App " << r_url.error().message();
        break;
      }
      type_ = Type::WebView;
      data_ = r_url.move_as_ok();
      break;
    }
    case telegram_api::inlineButtonTypeCopy::ID: {
      auto type = telegram_api::move_object_as<telegram_api::inlineButtonTypeCopy>(keyboard_button->type_);
      type_ = Type::Copy;
      data_ = std::move(type->copy_text_);
      break;
    }
    case telegram_api::inlineButtonTypeDisabled::ID:
      type_ = Type::Disabled;
      break;
    default:
      LOG(ERROR) << "Unsupported inline keyboard button: " << to_string(keyboard_button->type_);
  }
  style_ = KeyboardButtonStyle(std::move(keyboard_button->style_));
  text_ = std::move(keyboard_button->text_);
}

Result<InlineKeyboardButton> InlineKeyboardButton::get_inline_keyboard_button(
    td_api::object_ptr<td_api::inlineKeyboardButton> &&button, bool switch_inline_buttons_allowed) {
  CHECK(button != nullptr);
  if (!clean_input_string(button->text_)) {
    return Status::Error(400, "Inline keyboard button text must be encoded in UTF-8");
  }
  if (button->text_.empty()) {
    return Status::Error(400, "Inline keyboard button text must be non-empty");
  }
  if (button->type_ == nullptr) {
    return Status::Error(400, "Inline keyboard button type must be non-empty");
  }

  InlineKeyboardButton keyboard_button;
  keyboard_button.text_ = std::move(button->text_);
  keyboard_button.style_ = KeyboardButtonStyle(std::move(button->style_), button->icon_custom_emoji_id_);

  switch (button->type_->get_id()) {
    case td_api::inlineKeyboardButtonTypeUrl::ID: {
      auto button_type = td_api::move_object_as<td_api::inlineKeyboardButtonTypeUrl>(button->type_);
      auto user_id = LinkManager::get_link_user_id(button_type->url_);
      if (user_id.is_valid()) {
        keyboard_button.type_ = Type::User;
        keyboard_button.user_id_ = user_id;
        break;
      }
      auto r_url = LinkManager::check_link(button_type->url_);
      if (r_url.is_error()) {
        return Status::Error(400, PSLICE() << "Inline keyboard button " << r_url.error().message());
      }
      keyboard_button.type_ = Type::Url;
      keyboard_button.data_ = r_url.move_as_ok();
      if (!clean_input_string(keyboard_button.data_)) {
        return Status::Error(400, "Inline keyboard button URL must be encoded in UTF-8");
      }
      break;
    }
    case td_api::inlineKeyboardButtonTypeCallback::ID: {
      auto button_type = td_api::move_object_as<td_api::inlineKeyboardButtonTypeCallback>(button->type_);
      keyboard_button.type_ = Type::Callback;
      keyboard_button.data_ = std::move(button_type->data_);
      break;
    }
    case td_api::inlineKeyboardButtonTypeCallbackGame::ID:
      keyboard_button.type_ = Type::CallbackGame;
      break;
    case td_api::inlineKeyboardButtonTypeCallbackWithPassword::ID:
      return Status::Error(400, "Can't use CallbackWithPassword inline button");
    case td_api::inlineKeyboardButtonTypeSwitchInline::ID: {
      auto button_type = td_api::move_object_as<td_api::inlineKeyboardButtonTypeSwitchInline>(button->type_);
      if (button_type->target_chat_ == nullptr) {
        return Status::Error(400, "Target chat must be non-empty");
      }
      switch (button_type->target_chat_->get_id()) {
        case td_api::targetChatChosen::ID: {
          TRY_RESULT(types,
                     TargetDialogTypes::get_target_dialog_types(
                         static_cast<const td_api::targetChatChosen *>(button_type->target_chat_.get())->types_));
          keyboard_button.id_ = types.get_mask();
          keyboard_button.type_ = Type::SwitchInline;
          break;
        }
        case td_api::targetChatCurrent::ID:
          keyboard_button.type_ = Type::SwitchInlineCurrentDialog;
          break;
        case td_api::targetChatInternalLink::ID:
          return Status::Error(400, "Unsupported target chat specified");
        default:
          UNREACHABLE();
      }
      if (!switch_inline_buttons_allowed) {
        const char *button_name =
            keyboard_button.type_ == Type::SwitchInline ? "switch_inline_query" : "switch_inline_query_current_chat";
        return Status::Error(400, PSLICE() << "Can't use " << button_name
                                           << " button in a channel chat, because users will not be able to use the "
                                              "button without knowing bot's username");
      }

      keyboard_button.data_ = std::move(button_type->query_);
      if (!clean_input_string(keyboard_button.data_)) {
        return Status::Error(400, "Inline keyboard button switch inline query must be encoded in UTF-8");
      }
      break;
    }
    case td_api::inlineKeyboardButtonTypeBuy::ID:
      keyboard_button.type_ = Type::Buy;
      break;
    case td_api::inlineKeyboardButtonTypeLoginUrl::ID: {
      auto button_type = td_api::move_object_as<td_api::inlineKeyboardButtonTypeLoginUrl>(button->type_);
      auto user_id = LinkManager::get_link_user_id(button_type->url_);
      if (user_id.is_valid()) {
        return Status::Error(400, "Link to a user can't be used in login URL buttons");
      }
      auto r_url = LinkManager::check_link(button_type->url_, true, !G()->is_test_dc());
      if (r_url.is_error()) {
        return Status::Error(400, PSLICE() << "Inline keyboard button login " << r_url.error().message());
      }
      keyboard_button.type_ = Type::UrlAuth;
      keyboard_button.data_ = r_url.move_as_ok();
      keyboard_button.forward_text_ = std::move(button_type->forward_text_);
      if (!clean_input_string(keyboard_button.data_)) {
        return Status::Error(400, "Inline keyboard button login URL must be encoded in UTF-8");
      }
      if (!clean_input_string(keyboard_button.forward_text_)) {
        return Status::Error(400, "Inline keyboard button forward text must be encoded in UTF-8");
      }
      keyboard_button.id_ = button_type->id_;
      if (keyboard_button.id_ == std::numeric_limits<int64>::min()) {
        return Status::Error(400, "Invalid bot_user_id specified");
      }
      auto bot_user_id = UserId(keyboard_button.id_ >= 0 ? keyboard_button.id_ : -keyboard_button.id_ - 1);
      if (!bot_user_id.is_valid() && bot_user_id != UserId()) {
        return Status::Error(400, "Invalid bot_user_id specified");
      }
      break;
    }
    case td_api::inlineKeyboardButtonTypeUser::ID: {
      auto button_type = td_api::move_object_as<td_api::inlineKeyboardButtonTypeUser>(button->type_);
      keyboard_button.type_ = Type::User;
      keyboard_button.user_id_ = UserId(button_type->user_id_);
      if (!keyboard_button.user_id_.is_valid()) {
        return Status::Error(400, "Invalid user_id specified");
      }
      break;
    }
    case td_api::inlineKeyboardButtonTypeWebApp::ID: {
      auto button_type = td_api::move_object_as<td_api::inlineKeyboardButtonTypeWebApp>(button->type_);
      auto user_id = LinkManager::get_link_user_id(button_type->url_);
      if (user_id.is_valid()) {
        return Status::Error(400, "Link to a user can't be used in Web App URL buttons");
      }
      auto r_url = LinkManager::check_link(button_type->url_, true, !G()->is_test_dc());
      if (r_url.is_error()) {
        return Status::Error(400, PSLICE() << "Inline keyboard button Web App " << r_url.error().message());
      }
      keyboard_button.type_ = Type::WebView;
      keyboard_button.data_ = r_url.move_as_ok();
      if (!clean_input_string(keyboard_button.data_)) {
        return Status::Error(400, "Inline keyboard button Web App URL must be encoded in UTF-8");
      }
      break;
    }
    case td_api::inlineKeyboardButtonTypeCopyText::ID: {
      auto button_type = td_api::move_object_as<td_api::inlineKeyboardButtonTypeCopyText>(button->type_);
      keyboard_button.type_ = Type::Copy;
      keyboard_button.data_ = std::move(button_type->text_);
      if (!clean_input_string(keyboard_button.data_)) {
        return Status::Error(400, "Inline keyboard button copied text must be encoded in UTF-8");
      }
      break;
    }
    case td_api::inlineKeyboardButtonTypeDisabled::ID:
      keyboard_button.type_ = Type::Disabled;
      break;
    default:
      UNREACHABLE();
  }

  return std::move(keyboard_button);
}

telegram_api::object_ptr<telegram_api::keyboardInlineButton> InlineKeyboardButton::get_input_keyboard_inline_button(
    const UserManager *user_manager) const {
  auto type = [&]() -> telegram_api::object_ptr<telegram_api::InlineButtonType> {
    switch (type_) {
      case Type::Url:
        return telegram_api::make_object<telegram_api::inlineButtonTypeUrl>(data_);
      case Type::Callback:
        return telegram_api::make_object<telegram_api::inlineButtonTypeCallback>(0, false, BufferSlice(data_));
      case Type::CallbackGame:
        return telegram_api::make_object<telegram_api::inlineButtonTypeGame>();
      case Type::SwitchInline: {
        int32 flags = 0;
        auto peer_types = TargetDialogTypes(id_).get_input_peer_types();
        if (!peer_types.empty()) {
          flags |= telegram_api::inlineButtonTypeSwitchInline::PEER_TYPES_MASK;
        }
        return telegram_api::make_object<telegram_api::inlineButtonTypeSwitchInline>(flags, false, data_,
                                                                                     std::move(peer_types));
      }
      case Type::SwitchInlineCurrentDialog:
        return telegram_api::make_object<telegram_api::inlineButtonTypeSwitchInline>(
            0, true, data_, vector<telegram_api::object_ptr<telegram_api::InlineQueryPeerType>>());
      case Type::Buy:
        return telegram_api::make_object<telegram_api::inlineButtonTypeBuy>();
      case Type::UrlAuth: {
        int32 flags = 0;
        bool request_write_access = false;
        int64 bot_user_id = id_;
        if (bot_user_id >= 0) {
          request_write_access = true;
        } else {
          bot_user_id = -bot_user_id - 1;
        }
        if (!forward_text_.empty()) {
          flags |= telegram_api::inputInlineButtonTypeUrlAuth::FWD_TEXT_MASK;
        }
        telegram_api::object_ptr<telegram_api::InputUser> input_user;
        if (bot_user_id != 0) {
          auto r_input_user = user_manager->get_input_user(UserId(bot_user_id));
          if (r_input_user.is_error()) {
            LOG(ERROR) << "Failed to get InputUser for " << bot_user_id << ": " << r_input_user.error();
            return telegram_api::make_object<telegram_api::inlineButtonTypeUrl>(data_);
          }
          flags |= telegram_api::inputInlineButtonTypeUrlAuth::BOT_MASK;
          input_user = r_input_user.move_as_ok();
        }
        return telegram_api::make_object<telegram_api::inputInlineButtonTypeUrlAuth>(
            flags, request_write_access, forward_text_, data_, std::move(input_user));
      }
      case Type::CallbackWithPassword:
        UNREACHABLE();
        break;
      case Type::User: {
        auto r_input_user = user_manager->get_input_user(user_id_);
        if (r_input_user.is_error()) {
          LOG(ERROR) << "Failed to get InputUser for " << user_id_ << ": " << r_input_user.error();
          r_input_user = telegram_api::make_object<telegram_api::inputUserEmpty>();
        }
        return telegram_api::make_object<telegram_api::inputInlineButtonTypeUserProfile>(r_input_user.move_as_ok());
      }
      case Type::WebView:
        return telegram_api::make_object<telegram_api::inlineButtonTypeWebView>(data_);
      case Type::Copy:
        return telegram_api::make_object<telegram_api::inlineButtonTypeCopy>(data_);
      case Type::Disabled:
        return telegram_api::make_object<telegram_api::inlineButtonTypeDisabled>();
      default:
        UNREACHABLE();
        return nullptr;
    }
  }();
  int32 flags = 0;
  auto style = style_.get_input_keyboard_button_style();
  if (style != nullptr) {
    flags |= 1 << 10;
  }
  return telegram_api::make_object<telegram_api::keyboardInlineButton>(flags, std::move(style), text_, std::move(type));
}

td_api::object_ptr<td_api::inlineKeyboardButton> InlineKeyboardButton::get_inline_keyboard_button_object(
    UserManager *user_manager) const {
  td_api::object_ptr<td_api::InlineKeyboardButtonType> type;
  switch (type_) {
    case Type::Url:
      type = td_api::make_object<td_api::inlineKeyboardButtonTypeUrl>(data_);
      break;
    case Type::Callback:
      type = td_api::make_object<td_api::inlineKeyboardButtonTypeCallback>(data_);
      break;
    case Type::CallbackGame:
      type = td_api::make_object<td_api::inlineKeyboardButtonTypeCallbackGame>();
      break;
    case Type::SwitchInline: {
      type = td_api::make_object<td_api::inlineKeyboardButtonTypeSwitchInline>(
          data_, td_api::make_object<td_api::targetChatChosen>(TargetDialogTypes(id_).get_target_chat_types_object()));
      break;
    }
    case Type::SwitchInlineCurrentDialog:
      type = td_api::make_object<td_api::inlineKeyboardButtonTypeSwitchInline>(
          data_, td_api::make_object<td_api::targetChatCurrent>());
      break;
    case Type::Buy:
      type = td_api::make_object<td_api::inlineKeyboardButtonTypeBuy>();
      break;
    case Type::UrlAuth:
      type = td_api::make_object<td_api::inlineKeyboardButtonTypeLoginUrl>(data_, id_, forward_text_);
      break;
    case Type::CallbackWithPassword:
      type = td_api::make_object<td_api::inlineKeyboardButtonTypeCallbackWithPassword>(data_);
      break;
    case Type::User: {
      bool need_user = user_manager != nullptr && !user_manager->is_user_bot(user_manager->get_my_id());
      auto user_id =
          need_user ? user_manager->get_user_id_object(user_id_, "get_inline_keyboard_button_object") : user_id_.get();
      type = td_api::make_object<td_api::inlineKeyboardButtonTypeUser>(user_id);
      break;
    }
    case Type::WebView:
      type = td_api::make_object<td_api::inlineKeyboardButtonTypeWebApp>(data_);
      break;
    case Type::Copy:
      type = td_api::make_object<td_api::inlineKeyboardButtonTypeCopyText>(data_);
      break;
    case Type::Disabled:
      type = td_api::make_object<td_api::inlineKeyboardButtonTypeDisabled>();
      break;
    default:
      UNREACHABLE();
      return nullptr;
  }
  return td_api::make_object<td_api::inlineKeyboardButton>(text_, style_.get_icon_custom_emoji_id().get(),
                                                           style_.get_button_style_object(), std::move(type));
}

}  // namespace td
