//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/StarGiftResalePrice.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void StarGiftResalePrice::store(StorerT &storer) const {
  bool has_amount = amount_ != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_amount);
  END_STORE_FLAGS();
  td::store(type_, storer);
  if (has_amount) {
    td::store(amount_, storer);
  }
}

template <class ParserT>
void StarGiftResalePrice::parse(ParserT &parser) {
  bool has_amount;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_amount);
  END_PARSE_FLAGS();
  td::parse(type_, parser);
  if (has_amount) {
    td::parse(amount_, parser);
  }
}

}  // namespace td
