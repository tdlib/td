//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

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

  StarGiftBackground(int32 center_color, int32 edge_color, int32 text_color)
      : center_color_(center_color), edge_color_(edge_color), text_color_(text_color) {
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
