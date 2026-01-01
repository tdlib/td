//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/SuggestedPost.h"

#include "td/telegram/SuggestedPostPrice.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void SuggestedPost::store(StorerT &storer) const {
  bool has_price = !price_.is_empty();
  bool has_schedule_date = schedule_date_ != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_accepted_);
  STORE_FLAG(is_rejected_);
  STORE_FLAG(has_price);
  STORE_FLAG(has_schedule_date);
  END_STORE_FLAGS();
  if (has_price) {
    td::store(price_, storer);
  }
  if (has_schedule_date) {
    td::store(schedule_date_, storer);
  }
}

template <class ParserT>
void SuggestedPost::parse(ParserT &parser) {
  bool has_price;
  bool has_schedule_date;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_accepted_);
  PARSE_FLAG(is_rejected_);
  PARSE_FLAG(has_price);
  PARSE_FLAG(has_schedule_date);
  END_PARSE_FLAGS();
  if (has_price) {
    td::parse(price_, parser);
  }
  if (has_schedule_date) {
    td::parse(schedule_date_, parser);
  }
}

}  // namespace td
