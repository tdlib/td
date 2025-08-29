//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
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

class SearchPostsFlood {
  int32 total_daily_ = 0;
  int32 remains_ = 0;
  int64 star_count_ = 0;
  int32 wait_till_ = 0;
  bool is_free_ = false;

  friend bool operator==(const SearchPostsFlood &lhs, const SearchPostsFlood &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const SearchPostsFlood &flood);

 public:
  explicit SearchPostsFlood(telegram_api::object_ptr<telegram_api::searchPostsFlood> &&flood);

  td_api::object_ptr<td_api::publicPostSearchLimits> get_public_post_search_limits_object() const;

  bool is_free() const {
    return is_free_;
  }
};

bool operator==(const SearchPostsFlood &lhs, const SearchPostsFlood &rhs);

inline bool operator!=(const SearchPostsFlood &lhs, const SearchPostsFlood &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const SearchPostsFlood &flood);

}  // namespace td
