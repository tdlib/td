//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/StarGift.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void StarGift::store(StorerT &storer) const {
  CHECK(is_valid());
  bool is_limited = availability_total_ != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_limited);
  END_STORE_FLAGS();
  td::store(id_, storer);
  td::store(sticker_file_id_, storer);
  td::store(star_count_, storer);
  if (is_limited) {
    td::store(availability_remains_, storer);
    td::store(availability_total_, storer);
  }
}

template <class ParserT>
void StarGift::parse(ParserT &parser) {
  bool is_limited;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_limited);
  END_PARSE_FLAGS();
  td::parse(id_, parser);
  td::parse(sticker_file_id_, parser);
  td::parse(star_count_, parser);
  if (is_limited) {
    td::parse(availability_remains_, parser);
    td::parse(availability_total_, parser);
  }
}

}  // namespace td
