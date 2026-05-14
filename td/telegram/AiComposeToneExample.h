//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageEntity.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class AiComposeToneExample {
  FormattedText from_text_;
  FormattedText to_text_;

  friend bool operator==(const AiComposeToneExample &lhs, const AiComposeToneExample &rhs);

 public:
  AiComposeToneExample() = default;

  explicit AiComposeToneExample(telegram_api::object_ptr<telegram_api::aiComposeToneExample> &&example);

  bool is_empty() const {
    return from_text_.text.empty() && to_text_.text.empty();
  }

  td_api::object_ptr<td_api::textCompositionStyleExample> get_text_composition_style_example_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const AiComposeToneExample &lhs, const AiComposeToneExample &rhs);

inline bool operator!=(const AiComposeToneExample &lhs, const AiComposeToneExample &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
