//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SuggestedPostPrice.h"

#include "td/telegram/StarAmount.h"
#include "td/telegram/TonAmount.h"

#include "td/utils/logging.h"

namespace td {

SuggestedPostPrice::SuggestedPostPrice(telegram_api::object_ptr<telegram_api::StarsAmount> &&amount_ptr) {
  if (amount_ptr == nullptr) {
    return;
  }
  switch (amount_ptr->get_id()) {
    case telegram_api::starsAmount::ID: {
      auto star_amount = StarAmount(telegram_api::move_object_as<telegram_api::starsAmount>(amount_ptr), false);
      if (star_amount.get_nanostar_count() != 0) {
        LOG(ERROR) << "Receive suggested post price of " << star_amount << " Telegram Stars";
      }
      type_ = Type::Star;
      amount_ = star_amount.get_star_count();
      break;
    }
    case telegram_api::starsTonAmount::ID: {
      auto ton_amount =
          TonAmount(telegram_api::move_object_as<telegram_api::starsTonAmount>(amount_ptr), false).get_ton_amount();
      if (ton_amount % TON_MULTIPLIER != 0) {
        LOG(ERROR) << "Receive suggested post price of " << ton_amount << " TONs";
      }
      type_ = Type::Ton;
      amount_ = ton_amount / TON_MULTIPLIER;
      break;
    }
    default:
      UNREACHABLE();
  }
}

telegram_api::object_ptr<telegram_api::StarsAmount> SuggestedPostPrice::get_input_stars_amount() const {
  switch (type_) {
    case Type::None:
      return nullptr;
    case Type::Star:
      return telegram_api::make_object<telegram_api::starsAmount>(amount_, 0);
    case Type::Ton:
      return telegram_api::make_object<telegram_api::starsTonAmount>(amount_ * TON_MULTIPLIER);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

bool operator==(const SuggestedPostPrice &lhs, const SuggestedPostPrice &rhs) {
  return lhs.type_ == rhs.type_ && lhs.amount_ == rhs.amount_;
}

bool operator!=(const SuggestedPostPrice &lhs, const SuggestedPostPrice &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const SuggestedPostPrice &amount) {
  switch (amount.type_) {
    case SuggestedPostPrice::Type::None:
      return string_builder << "[Free]";
    case SuggestedPostPrice::Type::Star:
      return string_builder << '[' << amount.amount_ << " Stars]";
    case SuggestedPostPrice::Type::Ton:
      return string_builder << '[' << amount.amount_ << " toncoin cents]";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
