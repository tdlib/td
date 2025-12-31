//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/StarAmount.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/TonAmount.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class CurrencyAmount {
  enum class Type : int32 { None, Star, Ton };
  Type type_ = Type::None;
  StarAmount star_amount_;
  TonAmount ton_amount_;

  friend bool operator==(const CurrencyAmount &lhs, const CurrencyAmount &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const CurrencyAmount &amount);

 public:
  CurrencyAmount() = default;

  CurrencyAmount(telegram_api::object_ptr<telegram_api::StarsAmount> &&amount_ptr, bool allow_negative);

  StarAmount get_star_amount() const {
    return star_amount_;
  }

  TonAmount get_ton_amount() const {
    return ton_amount_;
  }

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const CurrencyAmount &lhs, const CurrencyAmount &rhs);
bool operator!=(const CurrencyAmount &lhs, const CurrencyAmount &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const CurrencyAmount &amount);

}  // namespace td
