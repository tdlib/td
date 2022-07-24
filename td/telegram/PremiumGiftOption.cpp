//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PremiumGiftOption.h"

#include "td/telegram/LinkManager.h"

#include "td/utils/common.h"

#include <limits>
#include <tuple>

namespace td {

PremiumGiftOption::PremiumGiftOption(telegram_api::object_ptr<telegram_api::premiumGiftOption> &&option)
    : months_(option->months_)
    , currency_(std::move(option->currency_))
    , amount_(option->amount_)
    , bot_url_(std::move(option->bot_url_))
    , store_product_(std::move(option->store_product_)) {
}

double PremiumGiftOption::get_monthly_price() const {
  return static_cast<double>(amount_) / static_cast<double>(months_);
}

td_api::object_ptr<td_api::premiumGiftOption> PremiumGiftOption::get_premium_gift_option_object(
    const PremiumGiftOption &base_option) const {
  auto link_type = LinkManager::parse_internal_link(bot_url_, true);
  int32 discount_percentage = 0;
  if (base_option.months_ > 0 && months_ > 0 && base_option.amount_ > 0 && amount_ > 0) {
    double relative_price = get_monthly_price() / base_option.get_monthly_price();
    if (relative_price < 1.0) {
      discount_percentage = static_cast<int32>(100 * (1.0 - relative_price));
    }
  }
  return td_api::make_object<td_api::premiumGiftOption>(
      currency_, amount_, discount_percentage, months_, store_product_,
      link_type == nullptr ? nullptr : link_type->get_internal_link_type_object());
}

bool operator<(const PremiumGiftOption &lhs, const PremiumGiftOption &rhs) {
  return std::tie(lhs.months_, lhs.amount_, lhs.currency_, lhs.store_product_, lhs.bot_url_) <
         std::tie(rhs.months_, rhs.amount_, rhs.currency_, rhs.store_product_, rhs.bot_url_);
}

bool operator==(const PremiumGiftOption &lhs, const PremiumGiftOption &rhs) {
  return lhs.months_ == rhs.months_ && lhs.currency_ == rhs.currency_ && lhs.amount_ == rhs.amount_ &&
         lhs.bot_url_ == rhs.bot_url_ && lhs.store_product_ == rhs.store_product_;
}

bool operator!=(const PremiumGiftOption &lhs, const PremiumGiftOption &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
