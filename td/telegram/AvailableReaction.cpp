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
  if (reaction[0] == '#') {
    return AvailableReactionType::NeedsPremium;
  }
  for (auto &available_reaction : available_reactions) {
    if (available_reaction.reaction_ == reaction) {
      return AvailableReactionType::Available;
    }
  }
  return AvailableReactionType::Unavailable;
}

ChatReactions get_active_reactions(const ChatReactions &available_reactions,
                                   const vector<AvailableReaction> &active_reactions) {
  if (available_reactions.reactions_.empty()) {
    // fast path
    return available_reactions;
  }
  CHECK(!available_reactions.allow_all_);
  CHECK(!available_reactions.allow_custom_);

  vector<string> result;
  for (const auto &active_reaction : active_reactions) {
    if (td::contains(available_reactions.reactions_, active_reaction.reaction_)) {
      result.push_back(active_reaction.reaction_);
    }
  }
  return ChatReactions(std::move(result));
}

bool operator==(const AvailableReaction &lhs, const AvailableReaction &rhs) {
  return lhs.reaction_ == rhs.reaction_;
}

}  // namespace td
