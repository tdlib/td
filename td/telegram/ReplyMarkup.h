//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/InlineKeyboardButton.h"
#include "td/telegram/KeyboardButton.h"
#include "td/telegram/MessageContentDupType.h"
#include "td/telegram/RequestedDialogType.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Dependencies;
class UserManager;

struct ReplyMarkup {
  // append only
  enum class Type : int32 { InlineKeyboard, ShowKeyboard, RemoveKeyboard, ForceReply };
  Type type;

  bool is_personal = false;  // for ShowKeyboard, RemoveKeyboard, ForceReply
  bool force_reply = false;  // for ShowKeyboard, InlineKeyboard

  bool is_persistent = false;               // for ShowKeyboard
  bool need_resize_keyboard = false;        // for ShowKeyboard
  bool is_one_time_keyboard = false;        // for ShowKeyboard
  vector<vector<KeyboardButton>> keyboard;  // for ShowKeyboard
  string placeholder;                       // for ShowKeyboard, ForceReply

  vector<vector<InlineKeyboardButton>> inline_keyboard;  // for InlineKeyboard

  StringBuilder &print(StringBuilder &string_builder) const;

  telegram_api::object_ptr<telegram_api::ReplyMarkup> get_input_reply_markup(UserManager *user_manager) const;

  td_api::object_ptr<td_api::ReplyMarkup> get_reply_markup_object(UserManager *user_manager) const;

  const RequestedDialogType *get_requested_dialog_type(int32 button_id) const;

  const string *get_login_button_url(int64 button_id) const;

  bool has_buy_button() const;
};

bool operator==(const ReplyMarkup &lhs, const ReplyMarkup &rhs);
bool operator!=(const ReplyMarkup &lhs, const ReplyMarkup &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const ReplyMarkup &reply_markup);

unique_ptr<ReplyMarkup> get_reply_markup(telegram_api::object_ptr<telegram_api::ReplyMarkup> &&reply_markup_ptr,
                                         bool is_bot, bool only_inline_keyboard, bool message_contains_mention);

Result<unique_ptr<ReplyMarkup>> get_inline_reply_markup(td_api::object_ptr<td_api::ReplyMarkup> &&reply_markup_ptr,
                                                        bool is_bot, bool switch_inline_buttons_allowed);

Result<unique_ptr<ReplyMarkup>> get_reply_markup(td_api::object_ptr<td_api::ReplyMarkup> &&reply_markup_ptr,
                                                 DialogType dialog_type, bool is_admined_monoforum, bool is_bot,
                                                 bool is_anonymous);

unique_ptr<ReplyMarkup> dup_reply_markup(const unique_ptr<ReplyMarkup> &reply_markup, DialogId dialog_id,
                                         const MessageContentDupType &dup_type, bool is_via_bot);

telegram_api::object_ptr<telegram_api::ReplyMarkup> get_input_reply_markup(UserManager *user_manager,
                                                                           const unique_ptr<ReplyMarkup> &reply_markup);

td_api::object_ptr<td_api::ReplyMarkup> get_reply_markup_object(UserManager *user_manager,
                                                                const unique_ptr<ReplyMarkup> &reply_markup);

void add_reply_markup_dependencies(Dependencies &dependencies, const ReplyMarkup *reply_markup);

}  // namespace td
