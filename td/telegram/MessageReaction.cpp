//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageReaction.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"

#include <unordered_set>

namespace td {

td_api::object_ptr<td_api::messageReaction> MessageReaction::get_message_reaction_object(Td *td) const {
  CHECK(!is_empty());

  vector<int64> recent_choosers;
  for (auto user_id : recent_chooser_user_ids_) {
    if (td->contacts_manager_->have_min_user(user_id)) {
      recent_choosers.push_back(td->contacts_manager_->get_user_id_object(user_id, "get_message_reaction_object"));
    } else {
      LOG(ERROR) << "Skip unknown reacted " << user_id;
    }
  }
  return td_api::make_object<td_api::messageReaction>(reaction_, choose_count_, is_chosen_, std::move(recent_choosers));
}

bool operator==(const MessageReaction &lhs, const MessageReaction &rhs) {
  return lhs.reaction_ == rhs.reaction_ && lhs.choose_count_ == rhs.choose_count_ && lhs.is_chosen_ == rhs.is_chosen_ &&
         lhs.recent_chooser_user_ids_ == rhs.recent_chooser_user_ids_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageReaction &reaction) {
  string_builder << '[' << reaction.reaction_ << (reaction.is_chosen_ ? " X " : " x ") << reaction.choose_count_;
  if (!reaction.recent_chooser_user_ids_.empty()) {
    string_builder << " by " << reaction.recent_chooser_user_ids_;
  }
  return string_builder << ']';
}

unique_ptr<MessageReactions> MessageReactions::get_message_reactions(
    Td *td, tl_object_ptr<telegram_api::messageReactions> &&reactions, bool is_bot) {
  if (reactions == nullptr || is_bot) {
    return nullptr;
  }

  auto result = make_unique<MessageReactions>();
  result->can_see_all_choosers_ = reactions->can_see_list_;
  result->is_min_ = reactions->min_;

  std::unordered_set<string> reaction_strings;
  for (auto &reaction_count : reactions->results_) {
    if (reaction_count->count_ <= 0 || reaction_count->count_ >= MessageReaction::MAX_CHOOSE_COUNT) {
      LOG(ERROR) << "Receive reaction " << reaction_count->reaction_ << " with invalid count "
                 << reaction_count->count_;
      continue;
    }

    if (!reaction_strings.insert(reaction_count->reaction_).second) {
      LOG(ERROR) << "Receive duplicate reaction " << reaction_count->reaction_;
      continue;
    }

    vector<UserId> recent_chooser_user_ids;
    for (auto &user_reaction : reactions->recent_reactons_) {
      if (user_reaction->reaction_ == reaction_count->reaction_) {
        UserId user_id(user_reaction->user_id_);
        if (!user_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << user_id;
          continue;
        }
        if (td::contains(recent_chooser_user_ids, user_id)) {
          LOG(ERROR) << "Receive duplicate " << user_id;
          continue;
        }
        if (!td->contacts_manager_->have_min_user(user_id)) {
          LOG(ERROR) << "Have no info about " << user_id;
          continue;
        }

        recent_chooser_user_ids.push_back(user_id);
        if (recent_chooser_user_ids.size() == MessageReaction::MAX_RECENT_CHOOSERS) {
          break;
        }
      }
    }

    result->reactions_.emplace_back(std::move(reaction_count->reaction_), reaction_count->count_,
                                    reaction_count->chosen_, std::move(recent_chooser_user_ids));
  }
  return result;
}

void MessageReactions::update_from(const MessageReactions &old_reactions) {
  if (old_reactions.has_pending_reaction_) {
    // we will ignore all updates, received while there is a pending reaction, so there are no reasons to update
    return;
  }
  CHECK(!has_pending_reaction_);

  if (is_min_ && !old_reactions.is_min_) {
    // chosen reaction was known, keep it
    is_min_ = false;
    for (const auto &old_reaction : old_reactions.reactions_) {
      if (old_reaction.is_chosen()) {
        for (auto &reaction : reactions_) {
          if (reaction.get_reaction() == old_reaction.get_reaction()) {
            reaction.set_is_chosen(true);
          }
        }
      }
    }
  }
}

bool MessageReactions::need_update_message_reactions(const MessageReactions *old_reactions,
                                                     const MessageReactions *new_reactions) {
  if (old_reactions == nullptr) {
    // add reactions
    return new_reactions != nullptr;
  }
  if (old_reactions->has_pending_reaction_) {
    // ignore all updates, received while there is a pending reaction
    return false;
  }
  if (new_reactions == nullptr) {
    // remove reactions when they are disabled
    return true;
  }

  // has_pending_reaction_ and old_chosen_reaction_ don't affect visible state
  // compare all other fields
  return old_reactions->reactions_ != new_reactions->reactions_ || old_reactions->is_min_ != new_reactions->is_min_ ||
         old_reactions->can_see_all_choosers_ != new_reactions->can_see_all_choosers_ ||
         old_reactions->need_polling_ != new_reactions->need_polling_;
}

}  // namespace td
