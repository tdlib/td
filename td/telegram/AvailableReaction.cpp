//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AvailableReaction.h"

#include "td/utils/algorithm.h"

namespace td {

AvailableReactionType get_reaction_type(const vector<string> &available_reactions, const string &reaction) {
  if (reaction[0] == '#') {
    return AvailableReactionType::NeedsPremium;
  }
  if (contains(available_reactions, reaction)) {
    return AvailableReactionType::Available;
  }
  return AvailableReactionType::Unavailable;
}

}  // namespace td
