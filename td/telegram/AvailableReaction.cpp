//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AvailableReaction.h"

#include "td/utils/algorithm.h"

namespace td {

AvailableReactionType get_reaction_type(const vector<AvailableReaction> &available_reactions, const string &reaction) {
  for (auto &available_reaction : available_reactions) {
    if (available_reaction.reaction_ == reaction) {
      if (available_reaction.is_premium_) {
        return AvailableReactionType::NeedsPremium;
      }
      return AvailableReactionType::Available;
    }
  }
  return AvailableReactionType::Unavailable;
}

vector<string> get_active_reactions(const vector<string> &available_reactions,
                                    const vector<AvailableReaction> &active_reactions) {
  if (available_reactions.empty()) {
    // fast path
    return available_reactions;
  }
  if (available_reactions.size() == active_reactions.size()) {
    size_t i;
    for (i = 0; i < available_reactions.size(); i++) {
      if (available_reactions[i] != active_reactions[i].reaction_) {
        break;
      }
    }
    if (i == available_reactions.size()) {
      // fast path
      return available_reactions;
    }
  }

  vector<string> result;
  for (const auto &active_reaction : active_reactions) {
    if (td::contains(available_reactions, active_reaction.reaction_)) {
      result.push_back(active_reaction.reaction_);
    }
  }
  return result;
}

td_api::object_ptr<td_api::availableReaction> AvailableReaction::get_available_reaction_object() const {
  return td_api::make_object<td_api::availableReaction>(reaction_, is_premium_);
}

bool operator==(const AvailableReaction &lhs, const AvailableReaction &rhs) {
  return lhs.reaction_ == rhs.reaction_ && lhs.is_premium_ == rhs.is_premium_;
}

}  // namespace td
