//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ReactionType.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/StringBuilder.h"

namespace td {

struct ChatReactions {
  vector<ReactionType> reaction_types_;
  bool allow_all_ = false;     // implies empty reaction_types_
  bool allow_custom_ = false;  // implies allow_all_

  ChatReactions() = default;

  explicit ChatReactions(vector<ReactionType> &&reactions) : reaction_types_(std::move(reactions)) {
  }

  explicit ChatReactions(telegram_api::object_ptr<telegram_api::ChatReactions> &&chat_reactions_ptr);

  ChatReactions(td_api::object_ptr<td_api::ChatAvailableReactions> &&chat_reactions_ptr, bool allow_custom);

  ChatReactions(bool allow_all, bool allow_custom) : allow_all_(allow_all), allow_custom_(allow_custom) {
  }

  ChatReactions get_active_reactions(
      const FlatHashMap<ReactionType, size_t, ReactionTypeHash> &active_reaction_pos) const;

  bool is_allowed_reaction_type(const ReactionType &reaction) const;

  telegram_api::object_ptr<telegram_api::ChatReactions> get_input_chat_reactions() const;

  td_api::object_ptr<td_api::ChatAvailableReactions> get_chat_available_reactions_object() const;

  bool empty() const {
    return reaction_types_.empty() && !allow_all_;
  }

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const ChatReactions &lhs, const ChatReactions &rhs);

inline bool operator!=(const ChatReactions &lhs, const ChatReactions &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const ChatReactions &reactions);

}  // namespace td
