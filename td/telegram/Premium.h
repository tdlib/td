//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"

namespace td {

class Td;

const vector<Slice> &get_premium_limit_keys();

void get_premium_limit(const td_api::object_ptr<td_api::PremiumLimitType> &limit_type,
                       Promise<td_api::object_ptr<td_api::premiumLimit>> &&promise);

void get_premium_features(Td *td, const td_api::object_ptr<td_api::PremiumSource> &source,
                          Promise<td_api::object_ptr<td_api::premiumFeatures>> &&promise);

void view_premium_feature(Td *td, const td_api::object_ptr<td_api::PremiumFeature> &feature, Promise<Unit> &&promise);

void click_premium_subscription_button(Td *td, Promise<Unit> &&promise);

void get_premium_state(Td *td, Promise<td_api::object_ptr<td_api::premiumState>> &&promise);

void can_purchase_premium(Td *td, Promise<Unit> &&promise);

void assign_app_store_transaction(Td *td, const string &receipt, bool is_restore, Promise<Unit> &&promise);

void assign_play_market_transaction(Td *td, const string &purchase_token, Promise<Unit> &&promise);

}  // namespace td
