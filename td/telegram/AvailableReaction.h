//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChatReactions.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"

namespace td {

enum class AvailableReactionType : int32 { Unavailable, Available, NeedsPremium };

AvailableReactionType get_reaction_type(const vector<string> &reactions, const string &reaction);

ChatReactions get_active_reactions(const ChatReactions &available_reactions, const vector<string> &active_reactions);

}  // namespace td
