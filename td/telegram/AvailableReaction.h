//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/utils/common.h"

namespace td {

struct AvailableReaction {
  string reaction_;
  bool is_premium_;

  AvailableReaction(const string &reaction, bool is_premium) : reaction_(reaction), is_premium_(is_premium) {
  }

  td_api::object_ptr<td_api::availableReaction> get_available_reaction_object() const;
};

bool operator==(const AvailableReaction &lhs, const AvailableReaction &rhs);

inline bool operator!=(const AvailableReaction &lhs, const AvailableReaction &rhs) {
  return !(lhs == rhs);
}

enum class AvailableReactionType : int32 { Unavailable, Available, NeedsPremium };

AvailableReactionType get_reaction_type(const vector<AvailableReaction> &reactions, const string &reaction);

vector<string> get_active_reactions(const vector<string> &available_reactions,
                                    const vector<AvailableReaction> &active_reactions);

}  // namespace td
