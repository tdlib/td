//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CurrencyAmount.h"
#include "td/telegram/StarAmount.hpp"
#include "td/telegram/TonAmount.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void CurrencyAmount::store(StorerT &storer) const {
  bool has_star_amount = star_amount_ != StarAmount();
  bool has_ton_amount = ton_amount_ != TonAmount();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_star_amount);
  STORE_FLAG(has_ton_amount);
  END_STORE_FLAGS();
  td::store(type_, storer);
  if (has_star_amount) {
    td::store(star_amount_, storer);
  }
  if (has_ton_amount) {
    td::store(ton_amount_, storer);
  }
}

template <class ParserT>
void CurrencyAmount::parse(ParserT &parser) {
  bool has_star_amount;
  bool has_ton_amount;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_star_amount);
  PARSE_FLAG(has_ton_amount);
  END_PARSE_FLAGS();
  td::parse(type_, parser);
  if (has_star_amount) {
    td::parse(star_amount_, parser);
  }
  if (has_ton_amount) {
    td::parse(ton_amount_, parser);
  }
}

}  // namespace td
