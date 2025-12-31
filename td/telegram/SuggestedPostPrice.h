//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

class SuggestedPostPrice {
  enum class Type : int32 { None, Star, Ton };
  Type type_ = Type::None;
  int64 amount_ = 0;

  static constexpr int64 TON_MULTIPLIER = 10000000;

  friend bool operator==(const SuggestedPostPrice &lhs, const SuggestedPostPrice &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const SuggestedPostPrice &amount);

 public:
  SuggestedPostPrice() = default;

  explicit SuggestedPostPrice(telegram_api::object_ptr<telegram_api::StarsAmount> &&amount_ptr);

  static Result<SuggestedPostPrice> get_suggested_post_price(const Td *td,
                                                             td_api::object_ptr<td_api::SuggestedPostPrice> &&price);

  bool is_empty() const {
    return type_ == Type::None;
  }

  telegram_api::object_ptr<telegram_api::StarsAmount> get_input_stars_amount() const;

  td_api::object_ptr<td_api::SuggestedPostPrice> get_suggested_post_price_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const SuggestedPostPrice &lhs, const SuggestedPostPrice &rhs);
bool operator!=(const SuggestedPostPrice &lhs, const SuggestedPostPrice &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const SuggestedPostPrice &amount);

}  // namespace td
