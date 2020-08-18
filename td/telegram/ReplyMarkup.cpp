//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ReplyMarkup.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/Td.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"

#include <limits>

namespace td {

static constexpr int32 REPLY_MARKUP_FLAG_NEED_RESIZE_KEYBOARD = 1 << 0;
static constexpr int32 REPLY_MARKUP_FLAG_IS_ONE_TIME_KEYBOARD = 1 << 1;
static constexpr int32 REPLY_MARKUP_FLAG_IS_PERSONAL = 1 << 2;

static bool operator==(const KeyboardButton &lhs, const KeyboardButton &rhs) {
  return lhs.type == rhs.type && lhs.text == rhs.text;
}

static StringBuilder &operator<<(StringBuilder &string_builder, const KeyboardButton &keyboard_button) {
  string_builder << "Button[";
  switch (keyboard_button.type) {
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
    default:
      UNREACHABLE();
  }
  return string_builder << ", " << keyboard_button.text << "]";
}

static bool operator==(const InlineKeyboardButton &lhs, const InlineKeyboardButton &rhs) {
  return lhs.type == rhs.type && lhs.text == rhs.text && lhs.data == rhs.data && lhs.id == rhs.id;
}

static StringBuilder &operator<<(StringBuilder &string_builder, const InlineKeyboardButton &keyboard_button) {
  string_builder << "Button[";
  switch (keyboard_button.type) {
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
      string_builder << "SwitchInline";
      break;
    case InlineKeyboardButton::Type::SwitchInlineCurrentDialog:
      string_builder << "SwitchInlineCurrentChat";
      break;
    case InlineKeyboardButton::Type::Buy:
      string_builder << "Buy";
      break;
    case InlineKeyboardButton::Type::UrlAuth:
      string_builder << "UrlAuth, id = " << keyboard_button.id;
      break;
    case InlineKeyboardButton::Type::CallbackWithPassword:
      string_builder << "CallbackWithPassword";
      break;
    default:
      UNREACHABLE();
  }
  return string_builder << ", text = " << keyboard_button.text << ", " << keyboard_button.data << "]";
}

bool operator==(const ReplyMarkup &lhs, const ReplyMarkup &rhs) {
  if (lhs.type != rhs.type) {
    return false;
  }
  if (lhs.type == ReplyMarkup::Type::InlineKeyboard) {
    return lhs.inline_keyboard == rhs.inline_keyboard;
  }

  if (lhs.is_personal != rhs.is_personal) {
    return false;
  }
  if (lhs.type != ReplyMarkup::Type::ShowKeyboard) {
    return true;
  }
  return lhs.need_resize_keyboard == rhs.need_resize_keyboard && lhs.is_one_time_keyboard == rhs.is_one_time_keyboard &&
         lhs.keyboard == rhs.keyboard;
}

bool operator!=(const ReplyMarkup &lhs, const ReplyMarkup &rhs) {
  return !(lhs == rhs);
}

StringBuilder &ReplyMarkup::print(StringBuilder &string_builder) const {
  string_builder << "ReplyMarkup[";
  switch (type) {
    case ReplyMarkup::Type::InlineKeyboard:
      string_builder << "InlineKeyboard";
      break;
    case ReplyMarkup::Type::ShowKeyboard:
      string_builder << "ShowKeyboard";
      break;
    case ReplyMarkup::Type::RemoveKeyboard:
      string_builder << "RemoveKeyboard";
      break;
    case ReplyMarkup::Type::ForceReply:
      string_builder << "ForceReply";
      break;
    default:
      UNREACHABLE();
  }
  if (is_personal) {
    string_builder << ", personal";
  }

  if (type == ReplyMarkup::Type::ShowKeyboard) {
    if (need_resize_keyboard) {
      string_builder << ", need resize";
    }
    if (is_one_time_keyboard) {
      string_builder << ", one time";
    }
  }
  if (type == ReplyMarkup::Type::InlineKeyboard) {
    for (auto &row : inline_keyboard) {
      string_builder << ", " << format::as_array(row);
    }
  }
  if (type == ReplyMarkup::Type::ShowKeyboard) {
    for (auto &row : keyboard) {
      string_builder << ", " << format::as_array(row);
    }
  }

  string_builder << "]";
  return string_builder;
}

StringBuilder &operator<<(StringBuilder &string_builder, const ReplyMarkup &reply_markup) {
  return reply_markup.print(string_builder);
}

static KeyboardButton get_keyboard_button(tl_object_ptr<telegram_api::KeyboardButton> &&keyboard_button_ptr) {
  CHECK(keyboard_button_ptr != nullptr);

  KeyboardButton button;
  switch (keyboard_button_ptr->get_id()) {
    case telegram_api::keyboardButton::ID: {
      auto keyboard_button = move_tl_object_as<telegram_api::keyboardButton>(keyboard_button_ptr);
      button.type = KeyboardButton::Type::Text;
      button.text = std::move(keyboard_button->text_);
      break;
    }
    case telegram_api::keyboardButtonRequestPhone::ID: {
      auto keyboard_button = move_tl_object_as<telegram_api::keyboardButtonRequestPhone>(keyboard_button_ptr);
      button.type = KeyboardButton::Type::RequestPhoneNumber;
      button.text = std::move(keyboard_button->text_);
      break;
    }
    case telegram_api::keyboardButtonRequestGeoLocation::ID: {
      auto keyboard_button = move_tl_object_as<telegram_api::keyboardButtonRequestGeoLocation>(keyboard_button_ptr);
      button.type = KeyboardButton::Type::RequestLocation;
      button.text = std::move(keyboard_button->text_);
      break;
    }
    case telegram_api::keyboardButtonRequestPoll::ID: {
      auto keyboard_button = move_tl_object_as<telegram_api::keyboardButtonRequestPoll>(keyboard_button_ptr);
      if (keyboard_button->flags_ & telegram_api::keyboardButtonRequestPoll::QUIZ_MASK) {
        if (keyboard_button->quiz_) {
          button.type = KeyboardButton::Type::RequestPollQuiz;
        } else {
          button.type = KeyboardButton::Type::RequestPollRegular;
        }
      } else {
        button.type = KeyboardButton::Type::RequestPoll;
      }
      button.text = std::move(keyboard_button->text_);
      break;
    }
    default:
      LOG(ERROR) << "Unsupported keyboard button: " << to_string(keyboard_button_ptr);
  }
  return button;
}

static InlineKeyboardButton get_inline_keyboard_button(
    tl_object_ptr<telegram_api::KeyboardButton> &&keyboard_button_ptr) {
  CHECK(keyboard_button_ptr != nullptr);

  InlineKeyboardButton button;
  switch (keyboard_button_ptr->get_id()) {
    case telegram_api::keyboardButtonUrl::ID: {
      auto keyboard_button = move_tl_object_as<telegram_api::keyboardButtonUrl>(keyboard_button_ptr);
      button.type = InlineKeyboardButton::Type::Url;
      button.text = std::move(keyboard_button->text_);
      button.data = std::move(keyboard_button->url_);
      break;
    }
    case telegram_api::keyboardButtonCallback::ID: {
      auto keyboard_button = move_tl_object_as<telegram_api::keyboardButtonCallback>(keyboard_button_ptr);
      button.type = keyboard_button->requires_password_ ? InlineKeyboardButton::Type::CallbackWithPassword
                                                        : InlineKeyboardButton::Type::Callback;
      button.text = std::move(keyboard_button->text_);
      button.data = keyboard_button->data_.as_slice().str();
      break;
    }
    case telegram_api::keyboardButtonGame::ID: {
      auto keyboard_button = move_tl_object_as<telegram_api::keyboardButtonGame>(keyboard_button_ptr);
      button.type = InlineKeyboardButton::Type::CallbackGame;
      button.text = std::move(keyboard_button->text_);
      break;
    }
    case telegram_api::keyboardButtonSwitchInline::ID: {
      auto keyboard_button = move_tl_object_as<telegram_api::keyboardButtonSwitchInline>(keyboard_button_ptr);
      button.type = (keyboard_button->flags_ & telegram_api::keyboardButtonSwitchInline::SAME_PEER_MASK) != 0
                        ? InlineKeyboardButton::Type::SwitchInlineCurrentDialog
                        : InlineKeyboardButton::Type::SwitchInline;
      button.text = std::move(keyboard_button->text_);
      button.data = std::move(keyboard_button->query_);
      break;
    }
    case telegram_api::keyboardButtonBuy::ID: {
      auto keyboard_button = move_tl_object_as<telegram_api::keyboardButtonBuy>(keyboard_button_ptr);
      button.type = InlineKeyboardButton::Type::Buy;
      button.text = std::move(keyboard_button->text_);
      break;
    }
    case telegram_api::keyboardButtonUrlAuth::ID: {
      auto keyboard_button = move_tl_object_as<telegram_api::keyboardButtonUrlAuth>(keyboard_button_ptr);
      button.type = InlineKeyboardButton::Type::UrlAuth;
      button.id = keyboard_button->button_id_;
      button.text = std::move(keyboard_button->text_);
      button.data = std::move(keyboard_button->url_);
      if ((keyboard_button->flags_ & telegram_api::keyboardButtonUrlAuth::FWD_TEXT_MASK) != 0) {
        button.forward_text = std::move(keyboard_button->fwd_text_);
      }
      break;
    }
    default:
      LOG(ERROR) << "Unsupported inline keyboard button: " << to_string(keyboard_button_ptr);
  }
  return button;
}

unique_ptr<ReplyMarkup> get_reply_markup(tl_object_ptr<telegram_api::ReplyMarkup> &&reply_markup_ptr, bool is_bot,
                                         bool only_inline_keyboard, bool message_contains_mention) {
  if (reply_markup_ptr == nullptr) {
    return nullptr;
  }

  auto reply_markup = make_unique<ReplyMarkup>();
  auto constructor_id = reply_markup_ptr->get_id();
  if (only_inline_keyboard && constructor_id != telegram_api::replyInlineMarkup::ID) {
    LOG(ERROR) << "Inline keyboard expected";
    return nullptr;
  }
  switch (constructor_id) {
    case telegram_api::replyInlineMarkup::ID: {
      auto inline_markup = move_tl_object_as<telegram_api::replyInlineMarkup>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::InlineKeyboard;
      reply_markup->inline_keyboard.reserve(inline_markup->rows_.size());
      for (auto &row : inline_markup->rows_) {
        vector<InlineKeyboardButton> buttons;
        buttons.reserve(row->buttons_.size());
        for (auto &button : row->buttons_) {
          buttons.push_back(get_inline_keyboard_button(std::move(button)));
          if (buttons.back().text.empty()) {
            buttons.pop_back();
          }
        }
        if (!buttons.empty()) {
          reply_markup->inline_keyboard.push_back(std::move(buttons));
        }
      }
      if (reply_markup->inline_keyboard.empty()) {
        return nullptr;
      }
      break;
    }
    case telegram_api::replyKeyboardMarkup::ID: {
      auto keyboard_markup = move_tl_object_as<telegram_api::replyKeyboardMarkup>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::ShowKeyboard;
      reply_markup->need_resize_keyboard = (keyboard_markup->flags_ & REPLY_MARKUP_FLAG_NEED_RESIZE_KEYBOARD) != 0;
      reply_markup->is_one_time_keyboard = (keyboard_markup->flags_ & REPLY_MARKUP_FLAG_IS_ONE_TIME_KEYBOARD) != 0;
      reply_markup->is_personal = (keyboard_markup->flags_ & REPLY_MARKUP_FLAG_IS_PERSONAL) != 0;
      reply_markup->keyboard.reserve(keyboard_markup->rows_.size());
      for (auto &row : keyboard_markup->rows_) {
        vector<KeyboardButton> buttons;
        buttons.reserve(row->buttons_.size());
        for (auto &button : row->buttons_) {
          buttons.push_back(get_keyboard_button(std::move(button)));
          if (buttons.back().text.empty()) {
            buttons.pop_back();
          }
        }
        if (!buttons.empty()) {
          reply_markup->keyboard.push_back(std::move(buttons));
        }
      }
      if (reply_markup->keyboard.empty()) {
        return nullptr;
      }
      break;
    }
    case telegram_api::replyKeyboardHide::ID: {
      auto hide_keyboard_markup = move_tl_object_as<telegram_api::replyKeyboardHide>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::RemoveKeyboard;
      reply_markup->is_personal = (hide_keyboard_markup->flags_ & REPLY_MARKUP_FLAG_IS_PERSONAL) != 0;
      break;
    }
    case telegram_api::replyKeyboardForceReply::ID: {
      auto force_reply_markup = move_tl_object_as<telegram_api::replyKeyboardForceReply>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::ForceReply;
      reply_markup->is_personal = (force_reply_markup->flags_ & REPLY_MARKUP_FLAG_IS_PERSONAL) != 0;
      break;
    }
    default:
      UNREACHABLE();
      return nullptr;
  }

  if (!is_bot && reply_markup->type != ReplyMarkup::Type::InlineKeyboard) {
    // incoming keyboard
    if (reply_markup->is_personal) {
      reply_markup->is_personal = message_contains_mention;
    } else {
      reply_markup->is_personal = true;
    }
  }

  return reply_markup;
}

static Result<KeyboardButton> get_keyboard_button(tl_object_ptr<td_api::keyboardButton> &&button,
                                                  bool request_buttons_allowed) {
  CHECK(button != nullptr);

  if (!clean_input_string(button->text_)) {
    return Status::Error(400, "Keyboard button text must be encoded in UTF-8");
  }

  KeyboardButton current_button;
  current_button.text = std::move(button->text_);

  int32 button_type_id = button->type_ == nullptr ? td_api::keyboardButtonTypeText::ID : button->type_->get_id();
  switch (button_type_id) {
    case td_api::keyboardButtonTypeText::ID:
      current_button.type = KeyboardButton::Type::Text;
      break;
    case td_api::keyboardButtonTypeRequestPhoneNumber::ID:
      if (!request_buttons_allowed) {
        return Status::Error(400, "Phone number can be requested in private chats only");
      }
      current_button.type = KeyboardButton::Type::RequestPhoneNumber;
      break;
    case td_api::keyboardButtonTypeRequestLocation::ID:
      if (!request_buttons_allowed) {
        return Status::Error(400, "Location can be requested in private chats only");
      }
      current_button.type = KeyboardButton::Type::RequestLocation;
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
        current_button.type = KeyboardButton::Type::RequestPollQuiz;
      } else if (request_poll->force_regular_) {
        current_button.type = KeyboardButton::Type::RequestPollRegular;
      } else {
        current_button.type = KeyboardButton::Type::RequestPoll;
      }
      break;
    }
    default:
      UNREACHABLE();
  }
  return current_button;
}

static Result<InlineKeyboardButton> get_inline_keyboard_button(tl_object_ptr<td_api::inlineKeyboardButton> &&button,
                                                               bool switch_inline_buttons_allowed) {
  CHECK(button != nullptr);
  if (!clean_input_string(button->text_)) {
    return Status::Error(400, "Keyboard button text must be encoded in UTF-8");
  }

  InlineKeyboardButton current_button;
  current_button.text = std::move(button->text_);

  if (button->type_ == nullptr) {
    return Status::Error(400, "Inline keyboard button type can't be empty");
  }

  int32 button_type_id = button->type_->get_id();
  switch (button_type_id) {
    case td_api::inlineKeyboardButtonTypeUrl::ID: {
      current_button.type = InlineKeyboardButton::Type::Url;
      TRY_RESULT_ASSIGN(current_button.data,
                        check_url(static_cast<const td_api::inlineKeyboardButtonTypeUrl *>(button->type_.get())->url_));
      if (!clean_input_string(current_button.data)) {
        return Status::Error(400, "Inline keyboard button url must be encoded in UTF-8");
      }
      break;
    }
    case td_api::inlineKeyboardButtonTypeCallback::ID:
      current_button.type = InlineKeyboardButton::Type::Callback;
      current_button.data =
          std::move(static_cast<td_api::inlineKeyboardButtonTypeCallback *>(button->type_.get())->data_);
      break;
    case td_api::inlineKeyboardButtonTypeCallbackGame::ID:
      current_button.type = InlineKeyboardButton::Type::CallbackGame;
      break;
    case td_api::inlineKeyboardButtonTypeCallbackWithPassword::ID:
      return Status::Error(400, "Can't use CallbackWithPassword inline button");
    case td_api::inlineKeyboardButtonTypeSwitchInline::ID: {
      auto switch_inline_button = move_tl_object_as<td_api::inlineKeyboardButtonTypeSwitchInline>(button->type_);
      if (!switch_inline_buttons_allowed) {
        const char *button_name =
            switch_inline_button->in_current_chat_ ? "switch_inline_query_current_chat" : "switch_inline_query";
        return Status::Error(400, PSLICE() << "Can't use " << button_name
                                           << " in a channel chat, because a user will not be able to use the button "
                                              "without knowing bot's username");
      }

      current_button.type = switch_inline_button->in_current_chat_
                                ? InlineKeyboardButton::Type::SwitchInlineCurrentDialog
                                : InlineKeyboardButton::Type::SwitchInline;
      current_button.data = std::move(switch_inline_button->query_);
      if (!clean_input_string(current_button.data)) {
        return Status::Error(400, "Inline keyboard button switch inline query must be encoded in UTF-8");
      }
      break;
    }
    case td_api::inlineKeyboardButtonTypeBuy::ID:
      current_button.type = InlineKeyboardButton::Type::Buy;
      break;
    case td_api::inlineKeyboardButtonTypeLoginUrl::ID: {
      current_button.type = InlineKeyboardButton::Type::UrlAuth;
      auto login_url = td_api::move_object_as<td_api::inlineKeyboardButtonTypeLoginUrl>(button->type_);
      TRY_RESULT_ASSIGN(current_button.data, check_url(login_url->url_));
      current_button.forward_text = std::move(login_url->forward_text_);
      if (!clean_input_string(current_button.data)) {
        return Status::Error(400, "Inline keyboard button login url must be encoded in UTF-8");
      }
      if (!clean_input_string(current_button.forward_text)) {
        return Status::Error(400, "Inline keyboard button forward text must be encoded in UTF-8");
      }
      current_button.id = login_url->id_;
      if (current_button.id == 0 || current_button.id == std::numeric_limits<int32>::min()) {
        return Status::Error(400, "Invalid bot_user_id specified");
      }
      break;
    }
    default:
      UNREACHABLE();
  }

  return current_button;
}

Result<unique_ptr<ReplyMarkup>> get_reply_markup(tl_object_ptr<td_api::ReplyMarkup> &&reply_markup_ptr, bool is_bot,
                                                 bool only_inline_keyboard, bool request_buttons_allowed,
                                                 bool switch_inline_buttons_allowed) {
  CHECK(!only_inline_keyboard || !request_buttons_allowed);
  if (reply_markup_ptr == nullptr || !is_bot) {
    return nullptr;
  }

  auto reply_markup = make_unique<ReplyMarkup>();
  auto constructor_id = reply_markup_ptr->get_id();
  if (only_inline_keyboard && constructor_id != td_api::replyMarkupInlineKeyboard::ID) {
    return Status::Error(400, "Inline keyboard expected");
  }

  switch (constructor_id) {
    case td_api::replyMarkupShowKeyboard::ID: {
      auto show_keyboard_markup = move_tl_object_as<td_api::replyMarkupShowKeyboard>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::ShowKeyboard;
      reply_markup->need_resize_keyboard = show_keyboard_markup->resize_keyboard_;
      reply_markup->is_one_time_keyboard = show_keyboard_markup->one_time_;
      reply_markup->is_personal = show_keyboard_markup->is_personal_;

      reply_markup->keyboard.reserve(show_keyboard_markup->rows_.size());
      int32 total_button_count = 0;
      for (auto &row : show_keyboard_markup->rows_) {
        vector<KeyboardButton> row_buttons;
        row_buttons.reserve(row.size());

        int32 row_button_count = 0;
        for (auto &button : row) {
          if (button->text_.empty()) {
            continue;
          }

          TRY_RESULT(current_button, get_keyboard_button(std::move(button), request_buttons_allowed));

          row_buttons.push_back(std::move(current_button));
          row_button_count++;
          total_button_count++;
          if (row_button_count >= 12 || total_button_count >= 300) {
            break;
          }
        }
        if (!row_buttons.empty()) {
          reply_markup->keyboard.push_back(row_buttons);
        }
        if (total_button_count >= 300) {
          break;
        }
      }
      if (reply_markup->keyboard.empty()) {
        return nullptr;
      }
      break;
    }
    case td_api::replyMarkupInlineKeyboard::ID: {
      auto inline_keyboard_markup = move_tl_object_as<td_api::replyMarkupInlineKeyboard>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::InlineKeyboard;

      reply_markup->inline_keyboard.reserve(inline_keyboard_markup->rows_.size());
      int32 total_button_count = 0;
      for (auto &row : inline_keyboard_markup->rows_) {
        vector<InlineKeyboardButton> row_buttons;
        row_buttons.reserve(row.size());

        int32 row_button_count = 0;
        for (auto &button : row) {
          if (button->text_.empty()) {
            continue;
          }

          TRY_RESULT(current_button, get_inline_keyboard_button(std::move(button), switch_inline_buttons_allowed));

          row_buttons.push_back(std::move(current_button));
          row_button_count++;
          total_button_count++;
          if (row_button_count >= 12 || total_button_count >= 300) {
            break;
          }
        }
        if (!row_buttons.empty()) {
          reply_markup->inline_keyboard.push_back(row_buttons);
        }
        if (total_button_count >= 300) {
          break;
        }
      }
      if (reply_markup->inline_keyboard.empty()) {
        return nullptr;
      }
      break;
    }
    case td_api::replyMarkupRemoveKeyboard::ID: {
      auto remove_keyboard_markup = move_tl_object_as<td_api::replyMarkupRemoveKeyboard>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::RemoveKeyboard;
      reply_markup->is_personal = remove_keyboard_markup->is_personal_;
      break;
    }
    case td_api::replyMarkupForceReply::ID: {
      auto force_reply_markup = move_tl_object_as<td_api::replyMarkupForceReply>(reply_markup_ptr);
      reply_markup->type = ReplyMarkup::Type::ForceReply;
      reply_markup->is_personal = force_reply_markup->is_personal_;
      break;
    }
    default:
      UNREACHABLE();
  }

  return std::move(reply_markup);
}

static tl_object_ptr<telegram_api::KeyboardButton> get_keyboard_button(const KeyboardButton &keyboard_button) {
  switch (keyboard_button.type) {
    case KeyboardButton::Type::Text:
      return make_tl_object<telegram_api::keyboardButton>(keyboard_button.text);
    case KeyboardButton::Type::RequestPhoneNumber:
      return make_tl_object<telegram_api::keyboardButtonRequestPhone>(keyboard_button.text);
    case KeyboardButton::Type::RequestLocation:
      return make_tl_object<telegram_api::keyboardButtonRequestGeoLocation>(keyboard_button.text);
    case KeyboardButton::Type::RequestPoll:
      return make_tl_object<telegram_api::keyboardButtonRequestPoll>(0, false, keyboard_button.text);
    case KeyboardButton::Type::RequestPollQuiz:
      return make_tl_object<telegram_api::keyboardButtonRequestPoll>(1, true, keyboard_button.text);
    case KeyboardButton::Type::RequestPollRegular:
      return make_tl_object<telegram_api::keyboardButtonRequestPoll>(1, false, keyboard_button.text);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

static tl_object_ptr<telegram_api::KeyboardButton> get_inline_keyboard_button(
    const InlineKeyboardButton &keyboard_button) {
  switch (keyboard_button.type) {
    case InlineKeyboardButton::Type::Url:
      return make_tl_object<telegram_api::keyboardButtonUrl>(keyboard_button.text, keyboard_button.data);
    case InlineKeyboardButton::Type::Callback:
      return make_tl_object<telegram_api::keyboardButtonCallback>(0, false /*ignored*/, keyboard_button.text,
                                                                  BufferSlice(keyboard_button.data));
    case InlineKeyboardButton::Type::CallbackGame:
      return make_tl_object<telegram_api::keyboardButtonGame>(keyboard_button.text);
    case InlineKeyboardButton::Type::SwitchInline:
    case InlineKeyboardButton::Type::SwitchInlineCurrentDialog: {
      int32 flags = 0;
      if (keyboard_button.type == InlineKeyboardButton::Type::SwitchInlineCurrentDialog) {
        flags |= telegram_api::keyboardButtonSwitchInline::SAME_PEER_MASK;
      }
      return make_tl_object<telegram_api::keyboardButtonSwitchInline>(flags, false /*ignored*/, keyboard_button.text,
                                                                      keyboard_button.data);
    }
    case InlineKeyboardButton::Type::Buy:
      return make_tl_object<telegram_api::keyboardButtonBuy>(keyboard_button.text);
    case InlineKeyboardButton::Type::UrlAuth: {
      int32 flags = 0;
      int32 bot_user_id = keyboard_button.id;
      if (bot_user_id > 0) {
        flags |= telegram_api::inputKeyboardButtonUrlAuth::REQUEST_WRITE_ACCESS_MASK;
      } else {
        bot_user_id = -bot_user_id;
      }
      if (!keyboard_button.forward_text.empty()) {
        flags |= telegram_api::inputKeyboardButtonUrlAuth::FWD_TEXT_MASK;
      }
      auto input_user = G()->td().get_actor_unsafe()->contacts_manager_->get_input_user(UserId(bot_user_id));
      if (input_user == nullptr) {
        LOG(ERROR) << "Failed to get InputUser for " << bot_user_id;
        return make_tl_object<telegram_api::keyboardButtonUrl>(keyboard_button.text, keyboard_button.data);
      }
      return make_tl_object<telegram_api::inputKeyboardButtonUrlAuth>(flags, false /*ignored*/, keyboard_button.text,
                                                                      keyboard_button.forward_text,
                                                                      keyboard_button.data, std::move(input_user));
    }
    case InlineKeyboardButton::Type::CallbackWithPassword:
      UNREACHABLE();
      break;
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<telegram_api::ReplyMarkup> ReplyMarkup::get_input_reply_markup() const {
  LOG(DEBUG) << "Send " << *this;
  switch (type) {
    case ReplyMarkup::Type::InlineKeyboard: {
      vector<tl_object_ptr<telegram_api::keyboardButtonRow>> rows;
      rows.reserve(inline_keyboard.size());
      for (auto &row : inline_keyboard) {
        vector<tl_object_ptr<telegram_api::KeyboardButton>> buttons;
        buttons.reserve(row.size());
        for (auto &button : row) {
          buttons.push_back(get_inline_keyboard_button(button));
        }
        rows.push_back(make_tl_object<telegram_api::keyboardButtonRow>(std::move(buttons)));
      }
      LOG(DEBUG) << "Return inlineKeyboardMarkup to send it";
      return make_tl_object<telegram_api::replyInlineMarkup>(std::move(rows));
    }
    case ReplyMarkup::Type::ShowKeyboard: {
      vector<tl_object_ptr<telegram_api::keyboardButtonRow>> rows;
      rows.reserve(keyboard.size());
      for (auto &row : keyboard) {
        vector<tl_object_ptr<telegram_api::KeyboardButton>> buttons;
        buttons.reserve(row.size());
        for (auto &button : row) {
          buttons.push_back(get_keyboard_button(button));
        }
        rows.push_back(make_tl_object<telegram_api::keyboardButtonRow>(std::move(buttons)));
      }
      LOG(DEBUG) << "Return replyKeyboardMarkup to send it";
      return make_tl_object<telegram_api::replyKeyboardMarkup>(
          need_resize_keyboard * REPLY_MARKUP_FLAG_NEED_RESIZE_KEYBOARD +
              is_one_time_keyboard * REPLY_MARKUP_FLAG_IS_ONE_TIME_KEYBOARD +
              is_personal * REPLY_MARKUP_FLAG_IS_PERSONAL,
          false /*ignored*/, false /*ignored*/, false /*ignored*/, std::move(rows));
    }
    case ReplyMarkup::Type::ForceReply:
      LOG(DEBUG) << "Return replyKeyboardForceReply to send it";
      return make_tl_object<telegram_api::replyKeyboardForceReply>(is_personal * REPLY_MARKUP_FLAG_IS_PERSONAL,
                                                                   false /*ignored*/, false /*ignored*/);
    case ReplyMarkup::Type::RemoveKeyboard:
      LOG(DEBUG) << "Return replyKeyboardHide to send it";
      return make_tl_object<telegram_api::replyKeyboardHide>(is_personal * REPLY_MARKUP_FLAG_IS_PERSONAL,
                                                             false /*ignored*/);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

static tl_object_ptr<td_api::keyboardButton> get_keyboard_button_object(const KeyboardButton &keyboard_button) {
  tl_object_ptr<td_api::KeyboardButtonType> type;
  switch (keyboard_button.type) {
    case KeyboardButton::Type::Text:
      type = make_tl_object<td_api::keyboardButtonTypeText>();
      break;
    case KeyboardButton::Type::RequestPhoneNumber:
      type = make_tl_object<td_api::keyboardButtonTypeRequestPhoneNumber>();
      break;
    case KeyboardButton::Type::RequestLocation:
      type = make_tl_object<td_api::keyboardButtonTypeRequestLocation>();
      break;
    case KeyboardButton::Type::RequestPoll:
      type = make_tl_object<td_api::keyboardButtonTypeRequestPoll>(false, false);
      break;
    case KeyboardButton::Type::RequestPollQuiz:
      type = make_tl_object<td_api::keyboardButtonTypeRequestPoll>(false, true);
      break;
    case KeyboardButton::Type::RequestPollRegular:
      type = make_tl_object<td_api::keyboardButtonTypeRequestPoll>(true, false);
      break;
    default:
      UNREACHABLE();
      return nullptr;
  }
  return make_tl_object<td_api::keyboardButton>(keyboard_button.text, std::move(type));
}

static tl_object_ptr<td_api::inlineKeyboardButton> get_inline_keyboard_button_object(
    const InlineKeyboardButton &keyboard_button) {
  tl_object_ptr<td_api::InlineKeyboardButtonType> type;
  switch (keyboard_button.type) {
    case InlineKeyboardButton::Type::Url:
      type = make_tl_object<td_api::inlineKeyboardButtonTypeUrl>(keyboard_button.data);
      break;
    case InlineKeyboardButton::Type::Callback:
      type = make_tl_object<td_api::inlineKeyboardButtonTypeCallback>(keyboard_button.data);
      break;
    case InlineKeyboardButton::Type::CallbackGame:
      type = make_tl_object<td_api::inlineKeyboardButtonTypeCallbackGame>();
      break;
    case InlineKeyboardButton::Type::SwitchInline:
      type = make_tl_object<td_api::inlineKeyboardButtonTypeSwitchInline>(keyboard_button.data, false);
      break;
    case InlineKeyboardButton::Type::SwitchInlineCurrentDialog:
      type = make_tl_object<td_api::inlineKeyboardButtonTypeSwitchInline>(keyboard_button.data, true);
      break;
    case InlineKeyboardButton::Type::Buy:
      type = make_tl_object<td_api::inlineKeyboardButtonTypeBuy>();
      break;
    case InlineKeyboardButton::Type::UrlAuth:
      type = make_tl_object<td_api::inlineKeyboardButtonTypeLoginUrl>(keyboard_button.data, keyboard_button.id,
                                                                      keyboard_button.forward_text);
      break;
    case InlineKeyboardButton::Type::CallbackWithPassword:
      type = make_tl_object<td_api::inlineKeyboardButtonTypeCallbackWithPassword>(keyboard_button.data);
      break;
    default:
      UNREACHABLE();
      return nullptr;
  }
  return make_tl_object<td_api::inlineKeyboardButton>(keyboard_button.text, std::move(type));
}

tl_object_ptr<td_api::ReplyMarkup> ReplyMarkup::get_reply_markup_object() const {
  switch (type) {
    case ReplyMarkup::Type::InlineKeyboard: {
      vector<vector<tl_object_ptr<td_api::inlineKeyboardButton>>> rows;
      rows.reserve(inline_keyboard.size());
      for (auto &row : inline_keyboard) {
        vector<tl_object_ptr<td_api::inlineKeyboardButton>> buttons;
        buttons.reserve(row.size());
        for (auto &button : row) {
          buttons.push_back(get_inline_keyboard_button_object(button));
        }
        rows.push_back(std::move(buttons));
      }

      return make_tl_object<td_api::replyMarkupInlineKeyboard>(std::move(rows));
    }
    case ReplyMarkup::Type::ShowKeyboard: {
      vector<vector<tl_object_ptr<td_api::keyboardButton>>> rows;
      rows.reserve(keyboard.size());
      for (auto &row : keyboard) {
        vector<tl_object_ptr<td_api::keyboardButton>> buttons;
        buttons.reserve(row.size());
        for (auto &button : row) {
          buttons.push_back(get_keyboard_button_object(button));
        }
        rows.push_back(std::move(buttons));
      }

      return make_tl_object<td_api::replyMarkupShowKeyboard>(std::move(rows), need_resize_keyboard,
                                                             is_one_time_keyboard, is_personal);
    }
    case ReplyMarkup::Type::RemoveKeyboard:
      return make_tl_object<td_api::replyMarkupRemoveKeyboard>(is_personal);
    case ReplyMarkup::Type::ForceReply:
      return make_tl_object<td_api::replyMarkupForceReply>(is_personal);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<telegram_api::ReplyMarkup> get_input_reply_markup(const unique_ptr<ReplyMarkup> &reply_markup) {
  if (reply_markup == nullptr) {
    return nullptr;
  }

  return reply_markup->get_input_reply_markup();
}

tl_object_ptr<td_api::ReplyMarkup> get_reply_markup_object(const unique_ptr<ReplyMarkup> &reply_markup) {
  if (reply_markup == nullptr) {
    return nullptr;
  }

  return reply_markup->get_reply_markup_object();
}

}  // namespace td
