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

class StarGiftBackground {
  int32 center_color_ = 0;
  int32 edge_color_ = 0;
  int32 text_color_ = 0;

  friend bool operator==(const StarGiftBackground &lhs, const StarGiftBackground &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const StarGiftBackground &background);

 public:
  StarGiftBackground() = default;

  explicit StarGiftBackground(const telegram_api::object_ptr<telegram_api::starGiftBackground> &background)
      : center_color_(background->center_color_)
      , edge_color_(background->edge_color_)
      , text_color_(background->text_color_) {
  }

  td_api::object_ptr<td_api::giftBackground> get_gift_background_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const StarGiftBackground &lhs, const StarGiftBackground &rhs);

inline bool operator!=(const StarGiftBackground &lhs, const StarGiftBackground &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const StarGiftBackground &background);

}  // namespace td
