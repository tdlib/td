//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/StarGift.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/ThemeSettings.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Dependencies;
class Td;

class ChatTheme {
  enum class Type : int32 { Default, Emoji, Gift };
  Type type_ = Type::Default;

  string emoji_;  // for Emoji

  StarGift star_gift_;         // for Gift
  ThemeSettings light_theme_;  // for Gift
  ThemeSettings dark_theme_;   // for Gift

  friend bool operator==(const ChatTheme &lhs, const ChatTheme &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const ChatTheme &chat_theme);

 public:
  ChatTheme() = default;

  ChatTheme(Td *td, telegram_api::object_ptr<telegram_api::ChatTheme> theme);

  static ChatTheme emoji(string &&emoji);

  bool is_default() const {
    return type_ == Type::Default;
  }

  bool is_gift() const {
    return type_ == Type::Gift;
  }

  td_api::object_ptr<td_api::giftChatTheme> get_gift_chat_theme_object(Td *td) const;

  td_api::object_ptr<td_api::ChatTheme> get_chat_theme_object(Td *td) const;

  void add_dependencies(Dependencies &dependencies) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const ChatTheme &lhs, const ChatTheme &rhs);

inline bool operator!=(const ChatTheme &lhs, const ChatTheme &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const ChatTheme &chat_theme);

}  // namespace td
