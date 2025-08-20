//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/StarRating.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void StarRating::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_maximum_level_reached_);
  END_STORE_FLAGS();
  td::store(level_, storer);
  td::store(star_count_, storer);
  td::store(current_level_star_count_, storer);
  td::store(next_level_star_count_, storer);
}

template <class ParserT>
void StarRating::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_maximum_level_reached_);
  END_PARSE_FLAGS();
  td::parse(level_, parser);
  td::parse(star_count_, parser);
  td::parse(current_level_star_count_, parser);
  td::parse(next_level_star_count_, parser);
}

}  // namespace td
