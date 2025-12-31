//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarRating.h"

#include "td/telegram/StarManager.h"

namespace td {

StarRating::StarRating(telegram_api::object_ptr<telegram_api::starsRating> &&rating) {
  CHECK(rating != nullptr);
  level_ = rating->level_;
  star_count_ = StarManager::get_star_count(rating->stars_, true);
  current_level_star_count_ = StarManager::get_star_count(rating->current_level_stars_, true);
  next_level_star_count_ = StarManager::get_star_count(rating->next_level_stars_, true);
  is_maximum_level_reached_ = next_level_star_count_ == 0 && level_ > 0;
}

unique_ptr<StarRating> StarRating::get_star_rating(telegram_api::object_ptr<telegram_api::starsRating> &&rating) {
  if (rating == nullptr) {
    return nullptr;
  }
  return make_unique<StarRating>(std::move(rating));
}

td_api::object_ptr<td_api::userRating> StarRating::get_user_rating_object() const {
  return td_api::make_object<td_api::userRating>(level_, is_maximum_level_reached_, star_count_,
                                                 current_level_star_count_, next_level_star_count_);
}

bool operator==(const StarRating &lhs, const StarRating &rhs) {
  return lhs.level_ == rhs.level_ && lhs.star_count_ == rhs.star_count_ &&
         lhs.current_level_star_count_ == rhs.current_level_star_count_ &&
         lhs.next_level_star_count_ == rhs.next_level_star_count_ &&
         lhs.is_maximum_level_reached_ == rhs.is_maximum_level_reached_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const StarRating &rating) {
  return string_builder << "level " << rating.level_ << " with rating " << rating.star_count_;
}

}  // namespace td
