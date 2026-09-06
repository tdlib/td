//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/KeyboardButtonStyle.h"
#include "td/telegram/MessageContentDupType.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Dependencies;
class UserManager;

struct InlineKeyboardButton {
  // append only
  enum class Type : int32 {
    Url,
    Callback,
    CallbackGame,
    SwitchInline,
    SwitchInlineCurrentDialog,
    Buy,
    UrlAuth,
    CallbackWithPassword,
    User,
    WebView,
    Copy,
    Disabled
  };

  Type type = Type::Disabled;
  int64 id = 0;    // UrlAuth: button_id or (2 * request_write_access - 1) * bot_user_id + request_write_access - 1
                   // SwitchInline: mask of allowed target chats; 0 if any
  UserId user_id;  // User only
  KeyboardButtonStyle style;
  string text;
  string forward_text;  // UrlAuth only
  string data;

  InlineKeyboardButton copy() const;

  InlineKeyboardButton() = default;
  InlineKeyboardButton(const InlineKeyboardButton &) = delete;
  InlineKeyboardButton &operator=(const InlineKeyboardButton &) = delete;
  InlineKeyboardButton(InlineKeyboardButton &&) = default;
  InlineKeyboardButton &operator=(InlineKeyboardButton &&) = default;
  ~InlineKeyboardButton() = default;

  void add_dependencies(Dependencies &dependencies) const;

  InlineKeyboardButton clone(DialogId dialog_id, const MessageContentDupType &dup_type, bool is_via_bot,
                             bool is_rich_message) const;

  bool is_buy() const {
    return type == Type::Buy;
  }

  bool is_disabled() const {
    return type == Type::Disabled;
  }

  const string &get_forward_text() const {
    return forward_text;
  }

  const string *get_login_url(int64 button_id) const {
    if (type == Type::UrlAuth && id == button_id) {
      return &data;
    }
    return nullptr;
  }
};

bool operator==(const InlineKeyboardButton &lhs, const InlineKeyboardButton &rhs);

InlineKeyboardButton get_inline_keyboard_button(
    telegram_api::object_ptr<telegram_api::keyboardInlineButton> &&keyboard_button);

Result<InlineKeyboardButton> get_inline_keyboard_button(td_api::object_ptr<td_api::inlineKeyboardButton> &&button,
                                                        bool switch_inline_buttons_allowed);

telegram_api::object_ptr<telegram_api::keyboardInlineButton> get_input_keyboard_inline_button(
    const UserManager *user_manager, const InlineKeyboardButton &keyboard_button);

td_api::object_ptr<td_api::inlineKeyboardButton> get_inline_keyboard_button_object(
    UserManager *user_manager, const InlineKeyboardButton &keyboard_button);

StringBuilder &operator<<(StringBuilder &string_builder, const InlineKeyboardButton &keyboard_button);

}  // namespace td
