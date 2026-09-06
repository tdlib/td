//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/KeyboardButton.h"

#include "td/telegram/Global.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/misc.h"

#include "td/utils/logging.h"
#include "td/utils/SliceBuilder.h"

namespace td {

KeyboardButton::KeyboardButton(telegram_api::object_ptr<telegram_api::keyboardButton> &&keyboard_button) {
  CHECK(keyboard_button != nullptr);

  switch (keyboard_button->type_->get_id()) {
    case telegram_api::buttonTypeDefault::ID:
      type_ = Type::Text;
      break;
    case telegram_api::buttonTypeRequestPhone::ID:
      type_ = Type::RequestPhoneNumber;
      break;
    case telegram_api::buttonTypeRequestGeoLocation::ID:
      type_ = Type::RequestLocation;
      break;
    case telegram_api::buttonTypeRequestPoll::ID: {
      auto type = telegram_api::move_object_as<telegram_api::buttonTypeRequestPoll>(keyboard_button->type_);
      if ((type->flags_ & telegram_api::buttonTypeRequestPoll::QUIZ_MASK) != 0) {
        if (type->quiz_) {
          type_ = Type::RequestPollQuiz;
        } else {
          type_ = Type::RequestPollRegular;
        }
      } else {
        type_ = Type::RequestPoll;
      }
      break;
    }
    case telegram_api::buttonTypeSimpleWebView::ID: {
      auto type = telegram_api::move_object_as<telegram_api::buttonTypeSimpleWebView>(keyboard_button->type_);
      auto r_url = LinkManager::check_link(type->url_);
      if (r_url.is_error()) {
        LOG(ERROR) << "Keyboard Web App " << r_url.error().message();
        break;
      }

      type_ = Type::WebView;
      url_ = r_url.move_as_ok();
      break;
    }
    case telegram_api::buttonTypeRequestPeer::ID: {
      auto type = telegram_api::move_object_as<telegram_api::buttonTypeRequestPeer>(keyboard_button->type_);
      type_ = Type::RequestDialog;
      requested_dialog_type_ =
          td::make_unique<RequestedDialogType>(std::move(type->peer_type_), type->button_id_, type->max_quantity_);
      break;
    }
    default:
      LOG(ERROR) << "Unsupported keyboard button: " << to_string(keyboard_button->type_);
  }
  style_ = KeyboardButtonStyle(std::move(keyboard_button->style_));
  text_ = std::move(keyboard_button->text_);
}

KeyboardButton KeyboardButton::clone() const {
  KeyboardButton result;
  result.type_ = type_;
  result.style_ = style_;
  result.text_ = text_;
  result.url_ = url_;
  if (requested_dialog_type_ != nullptr) {
    result.requested_dialog_type_ = td::make_unique<RequestedDialogType>(*requested_dialog_type_);
  }
  return result;
}

Result<KeyboardButton> KeyboardButton::get_keyboard_button(td_api::object_ptr<td_api::keyboardButton> &&button,
                                                           bool request_buttons_allowed) {
  CHECK(button != nullptr);

  if (!clean_input_string(button->text_)) {
    return Status::Error(400, "Keyboard button text must be encoded in UTF-8");
  }
  if (button->text_.empty()) {
    return Status::Error(400, "Keyboard button text must be non-empty");
  }

  KeyboardButton current_button;
  current_button.text_ = std::move(button->text_);
  current_button.style_ = KeyboardButtonStyle(std::move(button->style_), button->icon_custom_emoji_id_);

  switch (button->type_ == nullptr ? td_api::keyboardButtonTypeText::ID : button->type_->get_id()) {
    case td_api::keyboardButtonTypeText::ID:
      current_button.type_ = Type::Text;
      break;
    case td_api::keyboardButtonTypeRequestPhoneNumber::ID:
      if (!request_buttons_allowed) {
        return Status::Error(400, "Phone number can be requested in private chats only");
      }
      current_button.type_ = Type::RequestPhoneNumber;
      break;
    case td_api::keyboardButtonTypeRequestLocation::ID:
      if (!request_buttons_allowed) {
        return Status::Error(400, "Location can be requested in private chats only");
      }
      current_button.type_ = Type::RequestLocation;
      break;
    case td_api::keyboardButtonTypeRequestPoll::ID: {
      if (!request_buttons_allowed) {
        return Status::Error(400, "Poll can be requested in private chats only");
      }
      auto *request_poll = static_cast<const td_api::keyboardButtonTypeRequestPoll *>(button->type_.get());
      if (request_poll->force_quiz_ && request_poll->force_regular_) {
        return Status::Error(400, "Can't force quiz mode and regular poll simultaneously");
      }
      if (request_poll->force_quiz_) {
        current_button.type_ = Type::RequestPollQuiz;
      } else if (request_poll->force_regular_) {
        current_button.type_ = Type::RequestPollRegular;
      } else {
        current_button.type_ = Type::RequestPoll;
      }
      break;
    }
    case td_api::keyboardButtonTypeWebApp::ID: {
      if (!request_buttons_allowed) {
        return Status::Error(400, "Web App buttons can be used in private chats only");
      }

      auto button_type = move_tl_object_as<td_api::keyboardButtonTypeWebApp>(button->type_);
      auto user_id = LinkManager::get_link_user_id(button_type->url_);
      if (user_id.is_valid()) {
        return Status::Error(400, "Link to a user can't be used in Web App URL buttons");
      }
      auto r_url = LinkManager::check_link(button_type->url_, true, !G()->is_test_dc());
      if (r_url.is_error()) {
        return Status::Error(400, PSLICE() << "Keyboard button Web App " << r_url.error().message());
      }
      current_button.type_ = Type::WebView;
      current_button.url_ = std::move(button_type->url_);
      break;
    }
    case td_api::keyboardButtonTypeRequestUsers::ID: {
      if (!request_buttons_allowed) {
        return Status::Error(400, "Users can be requested in private chats only");
      }
      auto button_type = move_tl_object_as<td_api::keyboardButtonTypeRequestUsers>(button->type_);
      current_button.type_ = Type::RequestDialog;
      current_button.requested_dialog_type_ = td::make_unique<RequestedDialogType>(std::move(button_type));
      break;
    }
    case td_api::keyboardButtonTypeRequestChat::ID: {
      if (!request_buttons_allowed) {
        return Status::Error(400, "Chats can be requested in private chats only");
      }
      auto button_type = move_tl_object_as<td_api::keyboardButtonTypeRequestChat>(button->type_);
      current_button.type_ = Type::RequestDialog;
      current_button.requested_dialog_type_ = td::make_unique<RequestedDialogType>(std::move(button_type));
      break;
    }
    case td_api::keyboardButtonTypeRequestManagedBot::ID: {
      if (!request_buttons_allowed) {
        return Status::Error(400, "Managed bots can be requested in private chats only");
      }
      auto button_type = move_tl_object_as<td_api::keyboardButtonTypeRequestManagedBot>(button->type_);
      current_button.type_ = Type::RequestDialog;
      current_button.requested_dialog_type_ = td::make_unique<RequestedDialogType>(std::move(button_type));
      break;
    }
    default:
      UNREACHABLE();
  }
  return std::move(current_button);
}

telegram_api::object_ptr<telegram_api::keyboardButton> KeyboardButton::get_input_keyboard_button() const {
  auto type = [&]() -> telegram_api::object_ptr<telegram_api::ButtonType> {
    switch (type_) {
      case Type::Text:
        return telegram_api::make_object<telegram_api::buttonTypeDefault>();
      case Type::RequestPhoneNumber:
        return telegram_api::make_object<telegram_api::buttonTypeRequestPhone>();
      case Type::RequestLocation:
        return telegram_api::make_object<telegram_api::buttonTypeRequestGeoLocation>();
      case Type::RequestPoll:
        return telegram_api::make_object<telegram_api::buttonTypeRequestPoll>(0, false);
      case Type::RequestPollQuiz:
        return telegram_api::make_object<telegram_api::buttonTypeRequestPoll>(
            telegram_api::buttonTypeRequestPoll::QUIZ_MASK, true);
      case Type::RequestPollRegular:
        return telegram_api::make_object<telegram_api::buttonTypeRequestPoll>(
            telegram_api::buttonTypeRequestPoll::QUIZ_MASK, false);
      case Type::WebView:
        return telegram_api::make_object<telegram_api::buttonTypeSimpleWebView>(url_);
      case Type::RequestDialog:
        CHECK(requested_dialog_type_ != nullptr);
        return requested_dialog_type_->get_input_button_type_request_peer();
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
  return telegram_api::make_object<telegram_api::keyboardButton>(flags, std::move(style), text_, std::move(type));
}

td_api::object_ptr<td_api::keyboardButton> KeyboardButton::get_keyboard_button_object() const {
  td_api::object_ptr<td_api::KeyboardButtonType> type;
  switch (type_) {
    case Type::Text:
      type = make_tl_object<td_api::keyboardButtonTypeText>();
      break;
    case Type::RequestPhoneNumber:
      type = make_tl_object<td_api::keyboardButtonTypeRequestPhoneNumber>();
      break;
    case Type::RequestLocation:
      type = make_tl_object<td_api::keyboardButtonTypeRequestLocation>();
      break;
    case Type::RequestPoll:
      type = make_tl_object<td_api::keyboardButtonTypeRequestPoll>(false, false);
      break;
    case Type::RequestPollQuiz:
      type = make_tl_object<td_api::keyboardButtonTypeRequestPoll>(false, true);
      break;
    case Type::RequestPollRegular:
      type = make_tl_object<td_api::keyboardButtonTypeRequestPoll>(true, false);
      break;
    case Type::WebView:
      type = make_tl_object<td_api::keyboardButtonTypeWebApp>(url_ + "#kb");
      break;
    case Type::RequestDialog:
      type = requested_dialog_type_->get_keyboard_button_type_object();
      break;
    default:
      UNREACHABLE();
      return nullptr;
  }
  return td_api::make_object<td_api::keyboardButton>(text_, style_.get_icon_custom_emoji_id().get(),
                                                     style_.get_button_style_object(), std::move(type));
}

bool operator==(const KeyboardButton &lhs, const KeyboardButton &rhs) {
  return lhs.type_ == rhs.type_ && lhs.style_ == rhs.style_ && lhs.text_ == rhs.text_ && lhs.url_ == rhs.url_ &&
         lhs.requested_dialog_type_ == rhs.requested_dialog_type_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const KeyboardButton &keyboard_button) {
  string_builder << "Button[";
  switch (keyboard_button.type_) {
    case KeyboardButton::Type::Text:
      string_builder << "Text";
      break;
    case KeyboardButton::Type::RequestPhoneNumber:
      string_builder << "RequestPhoneNumber";
      break;
    case KeyboardButton::Type::RequestLocation:
      string_builder << "RequestLocation";
      break;
    case KeyboardButton::Type::RequestPoll:
      string_builder << "RequestPoll";
      break;
    case KeyboardButton::Type::RequestPollQuiz:
      string_builder << "RequestPollQuiz";
      break;
    case KeyboardButton::Type::RequestPollRegular:
      string_builder << "RequestPollRegular";
      break;
    case KeyboardButton::Type::WebView:
      string_builder << "WebApp";
      break;
    case KeyboardButton::Type::RequestDialog:
      string_builder << "RequestChat";
      break;
    default:
      UNREACHABLE();
  }
  return string_builder << ", " << keyboard_button.text_ << keyboard_button.style_ << ']';
}

}  // namespace td
