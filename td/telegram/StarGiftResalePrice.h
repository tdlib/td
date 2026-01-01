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

class StarGiftResalePrice {
  enum class Type : int32 { None, Star, Ton };
  Type type_ = Type::None;
  int64 amount_ = 0;

  static constexpr int64 TON_MULTIPLIER = 10000000;

  friend bool operator==(const StarGiftResalePrice &lhs, const StarGiftResalePrice &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const StarGiftResalePrice &amount);

 public:
  StarGiftResalePrice() = default;

  explicit StarGiftResalePrice(telegram_api::object_ptr<telegram_api::StarsAmount> &&amount_ptr);

  static Result<StarGiftResalePrice> get_star_gift_resale_price(const Td *td,
                                                                td_api::object_ptr<td_api::GiftResalePrice> &&price,
                                                                bool is_purchase);

  static StarGiftResalePrice legacy(int64 star_count);

  bool is_empty() const {
    return type_ == Type::None;
  }

  bool is_star() const {
    return type_ == Type::Star;
  }

  bool is_ton() const {
    return type_ == Type::Ton;
  }

  int64 get_star_count() const {
    return amount_;
  }

  int64 get_ton_count() const {
    return amount_ * TON_MULTIPLIER;
  }

  telegram_api::object_ptr<telegram_api::StarsAmount> get_input_stars_amount() const;

  td_api::object_ptr<td_api::GiftResalePrice> get_gift_resale_price_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const StarGiftResalePrice &lhs, const StarGiftResalePrice &rhs);
bool operator!=(const StarGiftResalePrice &lhs, const StarGiftResalePrice &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const StarGiftResalePrice &amount);

}  // namespace td
