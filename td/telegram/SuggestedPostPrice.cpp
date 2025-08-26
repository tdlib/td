//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SuggestedPostPrice.h"

#include "td/telegram/OptionManager.h"
#include "td/telegram/StarAmount.h"
#include "td/telegram/Td.h"
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
        LOG(ERROR) << "Receive price of " << star_amount << " Telegram Stars";
      }
      if (star_amount.get_star_count() == 0) {
        return;
      }
      type_ = Type::Star;
      amount_ = star_amount.get_star_count();
      break;
    }
    case telegram_api::starsTonAmount::ID: {
      auto ton_amount =
          TonAmount(telegram_api::move_object_as<telegram_api::starsTonAmount>(amount_ptr), false).get_ton_amount();
      if (ton_amount % TON_MULTIPLIER != 0) {
        LOG(ERROR) << "Receive price of " << ton_amount << " Toncoins";
      }
      ton_amount /= TON_MULTIPLIER;
      if (ton_amount == 0) {
        return;
      }
      type_ = Type::Ton;
      amount_ = ton_amount;
      break;
    }
    default:
      UNREACHABLE();
  }
}

Result<SuggestedPostPrice> SuggestedPostPrice::get_suggested_post_price(
    const Td *td, td_api::object_ptr<td_api::SuggestedPostPrice> &&price) {
  if (price == nullptr) {
    return SuggestedPostPrice();
  }
  switch (price->get_id()) {
    case td_api::suggestedPostPriceStar::ID: {
      auto amount = static_cast<const td_api::suggestedPostPriceStar *>(price.get())->star_count_;
      if (amount == 0) {
        return SuggestedPostPrice();
      }
      if (amount < td->option_manager_->get_option_integer("suggested_post_star_count_min") ||
          amount > td->option_manager_->get_option_integer("suggested_post_star_count_max")) {
        return Status::Error(400, "Invalid amount of Telegram Stars specified");
      }
      SuggestedPostPrice result;
      result.type_ = Type::Star;
      result.amount_ = amount;
      return result;
    }
    case td_api::suggestedPostPriceTon::ID: {
      auto amount = static_cast<const td_api::suggestedPostPriceTon *>(price.get())->toncoin_cent_count_;
      if (amount == 0) {
        return SuggestedPostPrice();
      }
      if (amount < td->option_manager_->get_option_integer("suggested_post_toncoin_cent_count_min") ||
          amount > td->option_manager_->get_option_integer("suggested_post_toncoin_cent_count_max")) {
        return Status::Error(400, "Invalid amount of Toncoin cents specified");
      }
      SuggestedPostPrice result;
      result.type_ = Type::Ton;
      result.amount_ = amount;
      return result;
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

td_api::object_ptr<td_api::SuggestedPostPrice> SuggestedPostPrice::get_suggested_post_price_object() const {
  switch (type_) {
    case Type::None:
      return nullptr;
    case Type::Star:
      return td_api::make_object<td_api::suggestedPostPriceStar>(amount_);
    case Type::Ton:
      return td_api::make_object<td_api::suggestedPostPriceTon>(amount_);
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
      return string_builder << '[' << amount.amount_ << " Toncoin cents]";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
