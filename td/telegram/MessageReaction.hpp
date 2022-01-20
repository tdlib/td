//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageReaction.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void MessageReaction::store(StorerT &storer) const {
  CHECK(!is_empty());
  bool has_recent_chooser_user_ids = !recent_chooser_user_ids_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_chosen_);
  STORE_FLAG(has_recent_chooser_user_ids);
  END_STORE_FLAGS();
  td::store(reaction_, storer);
  td::store(choose_count_, storer);
  if (has_recent_chooser_user_ids) {
    td::store(recent_chooser_user_ids_, storer);
  }
}

template <class ParserT>
void MessageReaction::parse(ParserT &parser) {
  bool has_recent_chooser_user_ids;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_chosen_);
  PARSE_FLAG(has_recent_chooser_user_ids);
  END_PARSE_FLAGS();
  td::parse(reaction_, parser);
  td::parse(choose_count_, parser);
  if (has_recent_chooser_user_ids) {
    td::parse(recent_chooser_user_ids_, parser);
  }
  CHECK(!is_empty());
}

template <class StorerT>
void MessageReactions::store(StorerT &storer) const {
  bool has_reactions = !reactions_.empty();
  bool has_old_chosen_reaction = !old_chosen_reaction_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_min_);
  STORE_FLAG(need_polling_);
  STORE_FLAG(can_see_all_choosers_);
  STORE_FLAG(has_pending_reaction_);
  STORE_FLAG(has_reactions);
  STORE_FLAG(has_old_chosen_reaction);
  END_STORE_FLAGS();
  if (has_reactions) {
    td::store(reactions_, storer);
  }
  if (has_old_chosen_reaction) {
    td::store(old_chosen_reaction_, storer);
  }
}

template <class ParserT>
void MessageReactions::parse(ParserT &parser) {
  bool has_reactions;
  bool has_old_chosen_reaction;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_min_);
  PARSE_FLAG(need_polling_);
  PARSE_FLAG(can_see_all_choosers_);
  PARSE_FLAG(has_pending_reaction_);
  PARSE_FLAG(has_reactions);
  PARSE_FLAG(has_old_chosen_reaction);
  END_PARSE_FLAGS();
  if (has_reactions) {
    td::parse(reactions_, parser);
  }
  if (has_old_chosen_reaction) {
    td::parse(old_chosen_reaction_, parser);
  }
}

}  // namespace td
