//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SearchPostsFlood.h"

#include "td/telegram/Global.h"
#include "td/telegram/StarManager.h"

namespace td {

SearchPostsFlood::SearchPostsFlood(telegram_api::object_ptr<telegram_api::searchPostsFlood> &&flood)
    : total_daily_(flood->total_daily_)
    , remains_(flood->remains_)
    , star_count_(StarManager::get_star_count(flood->stars_amount_))
    , wait_till_(flood->wait_till_)
    , is_free_(flood->query_is_free_) {
}

td_api::object_ptr<td_api::publicPostSearchLimits> SearchPostsFlood::get_public_post_search_limits_object() const {
  return td_api::make_object<td_api::publicPostSearchLimits>(
      total_daily_, remains_, max(0, wait_till_ - G()->unix_time()), star_count_, is_free_);
}

bool operator==(const SearchPostsFlood &lhs, const SearchPostsFlood &rhs) {
  return lhs.total_daily_ == rhs.total_daily_ && lhs.remains_ == rhs.remains_ && lhs.wait_till_ == rhs.wait_till_ &&
         lhs.star_count_ == rhs.star_count_ && lhs.is_free_ == rhs.is_free_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const SearchPostsFlood &flood) {
  if (flood.remains_ == 0) {
    string_builder << "Exhausted " << flood.total_daily_ << " free queries. Now, have to pay " << flood.star_count_
                   << " Stars till " << flood.wait_till_;
  } else {
    string_builder << "Have " << flood.remains_ << " left free queries out of " << flood.total_daily_;
  }
  return string_builder;
}

}  // namespace td
