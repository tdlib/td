//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PremiumGiftOption.h"

#include "td/telegram/LinkManager.h"

#include "td/utils/common.h"

namespace td {

PremiumGiftOption::PremiumGiftOption(telegram_api::object_ptr<telegram_api::premiumGiftOption> &&option)
    : months_(option->months_)
    , currency_(std::move(option->currency_))
    , amount_(option->amount_)
    , bot_url_(std::move(option->bot_url_))
    , store_product_(std::move(option->store_product_)) {
}

td_api::object_ptr<td_api::premiumGiftOption> PremiumGiftOption::get_premium_gift_option_object() const {
  auto link_type = LinkManager::parse_internal_link(bot_url_, true);
  return td_api::make_object<td_api::premiumGiftOption>(
      currency_, amount_, months_, store_product_,
      link_type == nullptr ? nullptr : link_type->get_internal_link_type_object());
}

bool operator==(const PremiumGiftOption &lhs, const PremiumGiftOption &rhs) {
  return lhs.months_ == rhs.months_ && lhs.currency_ == rhs.currency_ && lhs.amount_ == rhs.amount_ &&
         lhs.bot_url_ == rhs.bot_url_ && lhs.store_product_ == rhs.store_product_;
}

bool operator!=(const PremiumGiftOption &lhs, const PremiumGiftOption &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
