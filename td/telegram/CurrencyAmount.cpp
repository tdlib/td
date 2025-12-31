//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/CurrencyAmount.h"

namespace td {

CurrencyAmount::CurrencyAmount(telegram_api::object_ptr<telegram_api::StarsAmount> &&amount_ptr, bool allow_negative) {
  if (amount_ptr == nullptr) {
    return;
  }
  switch (amount_ptr->get_id()) {
    case telegram_api::starsAmount::ID: {
      auto star_amount =
          StarAmount(telegram_api::move_object_as<telegram_api::starsAmount>(amount_ptr), allow_negative);
      if (star_amount == StarAmount()) {
        return;
      }
      type_ = Type::Star;
      star_amount_ = star_amount;
      break;
    }
    case telegram_api::starsTonAmount::ID: {
      auto ton_amount =
          TonAmount(telegram_api::move_object_as<telegram_api::starsTonAmount>(amount_ptr), allow_negative);
      if (ton_amount == TonAmount()) {
        return;
      }
      type_ = Type::Ton;
      ton_amount_ = ton_amount;
      break;
    }
    default:
      UNREACHABLE();
  }
}

bool operator==(const CurrencyAmount &lhs, const CurrencyAmount &rhs) {
  return lhs.type_ == rhs.type_ && lhs.star_amount_ == rhs.star_amount_ && lhs.ton_amount_ == rhs.ton_amount_;
}

bool operator!=(const CurrencyAmount &lhs, const CurrencyAmount &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const CurrencyAmount &amount) {
  switch (amount.type_) {
    case CurrencyAmount::Type::None:
      return string_builder << "[Free]";
    case CurrencyAmount::Type::Star:
      return string_builder << '[' << amount.star_amount_ << " Stars]";
    case CurrencyAmount::Type::Ton:
      return string_builder << '[' << amount.ton_amount_ << " nanotoncoins]";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
