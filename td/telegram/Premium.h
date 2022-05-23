//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"

namespace td {

const vector<Slice> &get_premium_limit_keys();

void get_premium_features(Promise<td_api::object_ptr<td_api::premiumFeatures>> &&promise);

}  // namespace td
