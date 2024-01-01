//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ChatReactions.h"

#include "td/utils/algorithm.h"

namespace td {

ChatReactions::ChatReactions(telegram_api::object_ptr<telegram_api::ChatReactions> &&chat_reactions_ptr) {
  if (chat_reactions_ptr == nullptr) {
    return;
  }
  switch (chat_reactions_ptr->get_id()) {
    case telegram_api::chatReactionsNone::ID:
      break;
    case telegram_api::chatReactionsAll::ID: {
      auto chat_reactions = move_tl_object_as<telegram_api::chatReactionsAll>(chat_reactions_ptr);
      allow_all_regular_ = true;
      allow_all_custom_ = chat_reactions->allow_custom_;
      break;
    }
    case telegram_api::chatReactionsSome::ID: {
      auto chat_reactions = move_tl_object_as<telegram_api::chatReactionsSome>(chat_reactions_ptr);
      reaction_types_ = ReactionType::get_reaction_types(chat_reactions->reactions_);
      break;
    }
    default:
      UNREACHABLE();
  }
}

ChatReactions::ChatReactions(td_api::object_ptr<td_api::ChatAvailableReactions> &&chat_reactions_ptr,
                             bool allow_all_custom) {
  if (chat_reactions_ptr == nullptr) {
    return;
  }
  switch (chat_reactions_ptr->get_id()) {
    case td_api::chatAvailableReactionsAll::ID:
      allow_all_regular_ = true;
      allow_all_custom_ = allow_all_custom;
      break;
    case td_api::chatAvailableReactionsSome::ID: {
      auto chat_reactions = move_tl_object_as<td_api::chatAvailableReactionsSome>(chat_reactions_ptr);
      reaction_types_ = ReactionType::get_reaction_types(chat_reactions->reactions_);
      break;
    }
    default:
      UNREACHABLE();
  }
}

ChatReactions ChatReactions::get_active_reactions(
    const FlatHashMap<ReactionType, size_t, ReactionTypeHash> &active_reaction_pos) const {
  ChatReactions result = *this;
  if (!reaction_types_.empty()) {
    CHECK(!allow_all_regular_);
    CHECK(!allow_all_custom_);
    td::remove_if(result.reaction_types_, [&](const ReactionType &reaction_type) {
      return !reaction_type.is_active_reaction(active_reaction_pos);
    });
  }
  return result;
}

bool ChatReactions::is_allowed_reaction_type(const ReactionType &reaction_type) const {
  CHECK(!allow_all_regular_);
  if (allow_all_custom_ && reaction_type.is_custom_reaction()) {
    return true;
  }
  return td::contains(reaction_types_, reaction_type);
}

td_api::object_ptr<td_api::ChatAvailableReactions> ChatReactions::get_chat_available_reactions_object() const {
  if (allow_all_regular_) {
    return td_api::make_object<td_api::chatAvailableReactionsAll>();
  }
  return td_api::make_object<td_api::chatAvailableReactionsSome>(
      ReactionType::get_reaction_types_object(reaction_types_));
}

telegram_api::object_ptr<telegram_api::ChatReactions> ChatReactions::get_input_chat_reactions() const {
  if (allow_all_regular_) {
    int32 flags = 0;
    if (allow_all_custom_) {
      flags |= telegram_api::chatReactionsAll::ALLOW_CUSTOM_MASK;
    }
    return telegram_api::make_object<telegram_api::chatReactionsAll>(flags, allow_all_custom_);
  }
  if (!reaction_types_.empty()) {
    return telegram_api::make_object<telegram_api::chatReactionsSome>(
        ReactionType::get_input_reactions(reaction_types_));
  }
  return telegram_api::make_object<telegram_api::chatReactionsNone>();
}

bool operator==(const ChatReactions &lhs, const ChatReactions &rhs) {
  // don't compare allow_all_custom_
  return lhs.reaction_types_ == rhs.reaction_types_ && lhs.allow_all_regular_ == rhs.allow_all_regular_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const ChatReactions &reactions) {
  if (reactions.allow_all_regular_) {
    if (reactions.allow_all_custom_) {
      return string_builder << "AllReactions";
    }
    return string_builder << "AllRegularReactions";
  }
  return string_builder << '[' << reactions.reaction_types_ << ']';
}

}  // namespace td
