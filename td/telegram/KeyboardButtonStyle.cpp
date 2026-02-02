//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/KeyboardButtonStyle.h"

namespace td {

KeyboardButtonStyle::KeyboardButtonStyle(td_api::object_ptr<td_api::ButtonStyle> &&style, int64 icon_custom_emoji_id)
    : icon_custom_emoji_id_(icon_custom_emoji_id) {
  if (style == nullptr) {
    return;
  }
  switch (style->get_id()) {
    case td_api::buttonStyleDefault::ID:
      break;
    case td_api::buttonStylePrimary::ID:
      type_ = Type::Primary;
      break;
    case td_api::buttonStyleDanger::ID:
      type_ = Type::Danger;
      break;
    case td_api::buttonStyleSuccess::ID:
      type_ = Type::Success;
      break;
    default:
      UNREACHABLE();
      break;
  }
}

KeyboardButtonStyle::KeyboardButtonStyle(telegram_api::object_ptr<telegram_api::keyboardButtonStyle> &&style) {
  if (style == nullptr) {
    return;
  }
  if (style->bg_primary_) {
    type_ = Type::Primary;
  } else if (style->bg_danger_) {
    type_ = Type::Danger;
  } else if (style->bg_success_) {
    type_ = Type::Success;
  }
  icon_custom_emoji_id_ = CustomEmojiId(style->icon_);
}

td_api::object_ptr<td_api::ButtonStyle> KeyboardButtonStyle::get_button_style_object() const {
  switch (type_) {
    case Type::Default:
      return td_api::make_object<td_api::buttonStyleDefault>();
    case Type::Primary:
      return td_api::make_object<td_api::buttonStylePrimary>();
    case Type::Danger:
      return td_api::make_object<td_api::buttonStyleDanger>();
    case Type::Success:
      return td_api::make_object<td_api::buttonStyleSuccess>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

telegram_api::object_ptr<telegram_api::keyboardButtonStyle> KeyboardButtonStyle::get_input_keyboard_button_style()
    const {
  if (is_default()) {
    return nullptr;
  }
  int32 flags = 0;
  if (icon_custom_emoji_id_.is_valid()) {
    flags |= telegram_api::keyboardButtonStyle::ICON_MASK;
  }
  return telegram_api::make_object<telegram_api::keyboardButtonStyle>(
      flags, type_ == Type::Primary, type_ == Type::Danger, type_ == Type::Success, icon_custom_emoji_id_.get());
}

bool operator==(const KeyboardButtonStyle &lhs, const KeyboardButtonStyle &rhs) {
  return lhs.type_ == rhs.type_ && lhs.icon_custom_emoji_id_ == rhs.icon_custom_emoji_id_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const KeyboardButtonStyle &style) {
  if (style.is_default()) {
    return string_builder;
  }
  string_builder << ", ";
  switch (style.type_) {
    case KeyboardButtonStyle::Type::Default:
      string_builder << "Default";
      break;
    case KeyboardButtonStyle::Type::Primary:
      string_builder << "Primary";
      break;
    case KeyboardButtonStyle::Type::Danger:
      string_builder << "Danger";
      break;
    case KeyboardButtonStyle::Type::Success:
      string_builder << "Success";
      break;
    default:
      UNREACHABLE();
  }
  if (style.icon_custom_emoji_id_.is_valid()) {
    string_builder << style.icon_custom_emoji_id_.get();
  }
  return string_builder;
}

}  // namespace td
