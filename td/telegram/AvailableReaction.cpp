//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AvailableReaction.h"

namespace td {

td_api::object_ptr<td_api::availableReaction> AvailableReaction::get_available_reaction_object() const {
  return td_api::make_object<td_api::availableReaction>(reaction_, is_premium_);
}

bool operator==(const AvailableReaction &lhs, const AvailableReaction &rhs) {
  return lhs.reaction_ == rhs.reaction_ && lhs.is_premium_ == rhs.is_premium_;
}

}  // namespace td
