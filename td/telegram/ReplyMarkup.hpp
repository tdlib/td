//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ReplyMarkup.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(KeyboardButton button, StorerT &storer) {
  store(button.type, storer);
  store(button.text, storer);
}

template <class ParserT>
void parse(KeyboardButton &button, ParserT &parser) {
  parse(button.type, parser);
  parse(button.text, parser);
}

template <class StorerT>
void store(InlineKeyboardButton button, StorerT &storer) {
  store(button.type, storer);
  if (button.type == InlineKeyboardButton::Type::UrlAuth) {
    store(button.id, storer);
  }
  store(button.text, storer);
  store(button.data, storer);
}

template <class ParserT>
void parse(InlineKeyboardButton &button, ParserT &parser) {
  parse(button.type, parser);
  if (button.type == InlineKeyboardButton::Type::UrlAuth) {
    parse(button.id, parser);
  }
  parse(button.text, parser);
  parse(button.data, parser);
}

template <class StorerT>
void store(const ReplyMarkup &reply_markup, StorerT &storer) {
  bool has_keyboard = !reply_markup.keyboard.empty();
  bool has_inline_keyboard = !reply_markup.inline_keyboard.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(reply_markup.is_personal);
  STORE_FLAG(reply_markup.need_resize_keyboard);
  STORE_FLAG(reply_markup.is_one_time_keyboard);
  STORE_FLAG(has_keyboard);
  STORE_FLAG(has_inline_keyboard);
  END_STORE_FLAGS();
  store(reply_markup.type, storer);
  if (has_keyboard) {
    store(reply_markup.keyboard, storer);
  }
  if (has_inline_keyboard) {
    store(reply_markup.inline_keyboard, storer);
  }
}

template <class ParserT>
void parse(ReplyMarkup &reply_markup, ParserT &parser) {
  bool has_keyboard = !reply_markup.keyboard.empty();
  bool has_inline_keyboard = !reply_markup.inline_keyboard.empty();
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(reply_markup.is_personal);
  PARSE_FLAG(reply_markup.need_resize_keyboard);
  PARSE_FLAG(reply_markup.is_one_time_keyboard);
  PARSE_FLAG(has_keyboard);
  PARSE_FLAG(has_inline_keyboard);
  END_PARSE_FLAGS();
  parse(reply_markup.type, parser);
  if (has_keyboard) {
    parse(reply_markup.keyboard, parser);
  }
  if (has_inline_keyboard) {
    parse(reply_markup.inline_keyboard, parser);
  }
}

}  // namespace td
