//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGiftResalePrice.h"

#include "td/telegram/OptionManager.h"
#include "td/telegram/StarAmount.h"
#include "td/telegram/Td.h"
#include "td/telegram/TonAmount.h"

#include "td/utils/logging.h"

namespace td {

StarGiftResalePrice::StarGiftResalePrice(telegram_api::object_ptr<telegram_api::StarsAmount> &&amount_ptr) {
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

Result<StarGiftResalePrice> StarGiftResalePrice::get_star_gift_resale_price(
    const Td *td, td_api::object_ptr<td_api::GiftResalePrice> &&price, bool is_purchase) {
  if (price == nullptr) {
    if (is_purchase) {
      return Status::Error(400, "Gift resale price must be non-empty");
    } else {
      return StarGiftResalePrice();
    }
  }
  switch (price->get_id()) {
    case td_api::giftResalePriceStar::ID: {
      auto amount = static_cast<const td_api::giftResalePriceStar *>(price.get())->star_count_;
      if (amount <= 0) {
        return Status::Error(400, "Invalid amount of Telegram Stars specified");
      }
      if (!is_purchase) {
        if (amount < td->option_manager_->get_option_integer("gift_resale_star_count_min") ||
            amount > td->option_manager_->get_option_integer("gift_resale_star_count_max")) {
          return Status::Error(400, "Invalid amount of Telegram Stars specified");
        }
      }
      StarGiftResalePrice result;
      result.type_ = Type::Star;
      result.amount_ = amount;
      return result;
    }
    case td_api::giftResalePriceTon::ID: {
      auto amount = static_cast<const td_api::giftResalePriceTon *>(price.get())->toncoin_cent_count_;
      if (amount <= 0) {
        return Status::Error(400, "Invalid amount of Toncoins specified");
      }
      if (!is_purchase) {
        if (amount < td->option_manager_->get_option_integer("gift_resale_toncoin_cent_count_min") ||
            amount > td->option_manager_->get_option_integer("gift_resale_toncoin_cent_count_max")) {
          return Status::Error(400, "Invalid amount of Toncoin cents specified");
        }
      }
      StarGiftResalePrice result;
      result.type_ = Type::Ton;
      result.amount_ = amount;
      return result;
    }
    default:
      UNREACHABLE();
  }
}

StarGiftResalePrice StarGiftResalePrice::legacy(int64 star_count) {
  StarGiftResalePrice result;
  result.type_ = Type::Star;
  result.amount_ = star_count;
  return result;
}

telegram_api::object_ptr<telegram_api::StarsAmount> StarGiftResalePrice::get_input_stars_amount() const {
  switch (type_) {
    case Type::None:
      return telegram_api::make_object<telegram_api::starsAmount>(0, 0);
    case Type::Star:
      return telegram_api::make_object<telegram_api::starsAmount>(amount_, 0);
    case Type::Ton:
      return telegram_api::make_object<telegram_api::starsTonAmount>(amount_ * 10000000);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::GiftResalePrice> StarGiftResalePrice::get_gift_resale_price_object() const {
  switch (type_) {
    case Type::None:
      return nullptr;
    case Type::Star:
      return td_api::make_object<td_api::giftResalePriceStar>(amount_);
    case Type::Ton:
      return td_api::make_object<td_api::giftResalePriceTon>(amount_);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

bool operator==(const StarGiftResalePrice &lhs, const StarGiftResalePrice &rhs) {
  return lhs.type_ == rhs.type_ && lhs.amount_ == rhs.amount_;
}

bool operator!=(const StarGiftResalePrice &lhs, const StarGiftResalePrice &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const StarGiftResalePrice &amount) {
  switch (amount.type_) {
    case StarGiftResalePrice::Type::None:
      return string_builder << "[Free]";
    case StarGiftResalePrice::Type::Star:
      return string_builder << '[' << amount.amount_ << " Stars]";
    case StarGiftResalePrice::Type::Ton:
      return string_builder << '[' << amount.amount_ << " Toncoin cents]";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
