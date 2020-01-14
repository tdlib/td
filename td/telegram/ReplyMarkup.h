//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

struct KeyboardButton {
  // append only
  enum class Type : int32 {
    Text,
    RequestPhoneNumber,
    RequestLocation,
    RequestPoll,
    RequestPollQuiz,
    RequestPollRegular
  };
  Type type;
  string text;
};

struct InlineKeyboardButton {
  // append only
  enum class Type : int32 { Url, Callback, CallbackGame, SwitchInline, SwitchInlineCurrentDialog, Buy, UrlAuth };
  Type type;
  int32 id = 0;  // UrlAuth only, button_id or (2 * request_write_access - 1) * bot_user_id
  string text;
  string forward_text;  // UrlAuth only
  string data;
};

struct ReplyMarkup {
  // append only
  enum class Type : int32 { InlineKeyboard, ShowKeyboard, RemoveKeyboard, ForceReply };
  Type type;

  bool is_personal = false;  // for ShowKeyboard, RemoveKeyboard, ForceReply

  bool need_resize_keyboard = false;        // for ShowKeyboard
  bool is_one_time_keyboard = false;        // for ShowKeyboard
  vector<vector<KeyboardButton>> keyboard;  // for ShowKeyboard

  vector<vector<InlineKeyboardButton>> inline_keyboard;  // for InlineKeyboard

  StringBuilder &print(StringBuilder &string_builder) const;

  tl_object_ptr<telegram_api::ReplyMarkup> get_input_reply_markup() const;

  tl_object_ptr<td_api::ReplyMarkup> get_reply_markup_object() const;
};

bool operator==(const ReplyMarkup &lhs, const ReplyMarkup &rhs);
bool operator!=(const ReplyMarkup &lhs, const ReplyMarkup &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const ReplyMarkup &reply_markup);

unique_ptr<ReplyMarkup> get_reply_markup(tl_object_ptr<telegram_api::ReplyMarkup> &&reply_markup_ptr, bool is_bot,
                                         bool only_inline_keyboard, bool message_contains_mention);

Result<unique_ptr<ReplyMarkup>> get_reply_markup(tl_object_ptr<td_api::ReplyMarkup> &&reply_markup_ptr, bool is_bot,
                                                 bool only_inline_keyboard, bool request_buttons_allowed,
                                                 bool switch_inline_buttons_allowed) TD_WARN_UNUSED_RESULT;

tl_object_ptr<telegram_api::ReplyMarkup> get_input_reply_markup(const unique_ptr<ReplyMarkup> &reply_markup);

tl_object_ptr<td_api::ReplyMarkup> get_reply_markup_object(const unique_ptr<ReplyMarkup> &reply_markup);

}  // namespace td
