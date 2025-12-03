//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGiftBackground.h"

namespace td {

td_api::object_ptr<td_api::giftBackground> StarGiftBackground::get_gift_background_object() const {
  return td_api::make_object<td_api::giftBackground>(center_color_, edge_color_, text_color_);
}

bool operator==(const StarGiftBackground &lhs, const StarGiftBackground &rhs) {
  return lhs.center_color_ == rhs.center_color_ && lhs.edge_color_ == rhs.edge_color_ &&
         lhs.text_color_ == rhs.text_color_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const StarGiftBackground &background) {
  return string_builder << "GiftBackground[" << background.center_color_ << '/' << background.edge_color_ << '/'
                        << background.text_color_ << ']';
}

}  // namespace td
