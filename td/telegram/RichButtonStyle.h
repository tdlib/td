//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class RichButtonStyle {
  enum class Type : int32 { Default, Primary, Danger, Success, Link };
  Type type_ = Type::Default;

  friend bool operator==(const RichButtonStyle &lhs, const RichButtonStyle &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const RichButtonStyle &style);

 public:
  RichButtonStyle() = default;

  explicit RichButtonStyle(td_api::object_ptr<td_api::ButtonStyle> &&style);

  explicit RichButtonStyle(telegram_api::object_ptr<telegram_api::richButtonStyle> &&style);

  bool is_default() const {
    return type_ == Type::Default;
  }

  td_api::object_ptr<td_api::ButtonStyle> get_button_style_object() const;

  telegram_api::object_ptr<telegram_api::richButtonStyle> get_input_rich_button_style() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const RichButtonStyle &lhs, const RichButtonStyle &rhs);

inline bool operator!=(const RichButtonStyle &lhs, const RichButtonStyle &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const RichButtonStyle &style);

}  // namespace td
