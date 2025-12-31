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

class StarRating {
  int32 level_ = 0;
  int64 star_count_ = 0;
  int64 current_level_star_count_ = 0;
  int64 next_level_star_count_ = 0;
  bool is_maximum_level_reached_ = false;

  friend bool operator==(const StarRating &lhs, const StarRating &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const StarRating &rating);

 public:
  StarRating() = default;

  explicit StarRating(telegram_api::object_ptr<telegram_api::starsRating> &&rating);

  static unique_ptr<StarRating> get_star_rating(telegram_api::object_ptr<telegram_api::starsRating> &&rating);

  td_api::object_ptr<td_api::userRating> get_user_rating_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const StarRating &lhs, const StarRating &rhs);

inline bool operator!=(const StarRating &lhs, const StarRating &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const StarRating &rating);

}  // namespace td
