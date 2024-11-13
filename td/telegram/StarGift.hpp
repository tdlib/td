//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Global.h"
#include "td/telegram/StarGift.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StickersManager.hpp"
#include "td/telegram/Td.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void StarGift::store(StorerT &storer) const {
  CHECK(is_valid());
  Td *td = storer.context()->td().get_actor_unsafe();
  bool is_limited = availability_total_ != 0;
  bool has_default_sell_star_count = default_sell_star_count_ != star_count_ * 85 / 100;
  bool has_first_sale_date = first_sale_date_ != 0;
  bool has_last_sale_date = last_sale_date_ != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_limited);
  STORE_FLAG(has_default_sell_star_count);
  STORE_FLAG(has_first_sale_date);
  STORE_FLAG(has_last_sale_date);
  STORE_FLAG(is_for_birthday_);
  END_STORE_FLAGS();
  td::store(id_, storer);
  td->stickers_manager_->store_sticker(sticker_file_id_, false, storer, "StarGift");
  td::store(star_count_, storer);
  if (is_limited) {
    td::store(availability_remains_, storer);
    td::store(availability_total_, storer);
  }
  if (has_default_sell_star_count) {
    td::store(default_sell_star_count_, storer);
  }
  if (has_first_sale_date) {
    td::store(first_sale_date_, storer);
  }
  if (has_last_sale_date) {
    td::store(last_sale_date_, storer);
  }
}

template <class ParserT>
void StarGift::parse(ParserT &parser) {
  Td *td = parser.context()->td().get_actor_unsafe();
  bool is_limited;
  bool has_default_sell_star_count;
  bool has_first_sale_date;
  bool has_last_sale_date;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_limited);
  PARSE_FLAG(has_default_sell_star_count);
  PARSE_FLAG(has_first_sale_date);
  PARSE_FLAG(has_last_sale_date);
  PARSE_FLAG(is_for_birthday_);
  END_PARSE_FLAGS();
  td::parse(id_, parser);
  sticker_file_id_ = td->stickers_manager_->parse_sticker(false, parser);
  td::parse(star_count_, parser);
  if (is_limited) {
    td::parse(availability_remains_, parser);
    td::parse(availability_total_, parser);
  }
  if (has_default_sell_star_count) {
    td::parse(default_sell_star_count_, parser);
  } else {
    default_sell_star_count_ = star_count_ * 85 / 100;
  }
  if (has_first_sale_date) {
    td::parse(first_sale_date_, parser);
  }
  if (has_last_sale_date) {
    td::parse(last_sale_date_, parser);
  }
}

}  // namespace td
