//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class KeyboardButtonStyle {
  enum class Type : int32 { Default, Primary, Danger, Success };
  Type type_ = Type::Default;
  CustomEmojiId icon_custom_emoji_id_;

  friend bool operator==(const KeyboardButtonStyle &lhs, const KeyboardButtonStyle &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const KeyboardButtonStyle &style);

 public:
  KeyboardButtonStyle() = default;

  KeyboardButtonStyle(td_api::object_ptr<td_api::ButtonStyle> &&style, int64 icon_custom_emoji_id);

  explicit KeyboardButtonStyle(telegram_api::object_ptr<telegram_api::keyboardButtonStyle> &&style);

  bool is_default() const {
    return type_ == Type::Default && !icon_custom_emoji_id_.is_valid();
  }

  td_api::object_ptr<td_api::ButtonStyle> get_button_style_object() const;

  CustomEmojiId get_icon_custom_emoji_id() const {
    return icon_custom_emoji_id_;
  }

  telegram_api::object_ptr<telegram_api::keyboardButtonStyle> get_input_keyboard_button_style() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const KeyboardButtonStyle &lhs, const KeyboardButtonStyle &rhs);

inline bool operator!=(const KeyboardButtonStyle &lhs, const KeyboardButtonStyle &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const KeyboardButtonStyle &style);

}  // namespace td
