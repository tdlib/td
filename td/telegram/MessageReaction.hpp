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
  bool has_recent_chooser_dialog_ids = !recent_chooser_dialog_ids_.empty();
  bool has_recent_chooser_min_channels = !recent_chooser_min_channels_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_chosen_);
  STORE_FLAG(has_recent_chooser_dialog_ids);
  STORE_FLAG(has_recent_chooser_min_channels);
  END_STORE_FLAGS();
  td::store(reaction_, storer);
  td::store(choose_count_, storer);
  if (has_recent_chooser_dialog_ids) {
    td::store(recent_chooser_dialog_ids_, storer);
  }
  if (has_recent_chooser_min_channels) {
    td::store(recent_chooser_min_channels_, storer);
  }
}

template <class ParserT>
void MessageReaction::parse(ParserT &parser) {
  bool has_recent_chooser_dialog_ids;
  bool has_recent_chooser_min_channels;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_chosen_);
  PARSE_FLAG(has_recent_chooser_dialog_ids);
  PARSE_FLAG(has_recent_chooser_min_channels);
  END_PARSE_FLAGS();
  td::parse(reaction_, parser);
  td::parse(choose_count_, parser);
  if (has_recent_chooser_dialog_ids) {
    td::parse(recent_chooser_dialog_ids_, parser);
  }
  if (has_recent_chooser_min_channels) {
    td::parse(recent_chooser_min_channels_, parser);
  }
  CHECK(!is_empty());
}

template <class StorerT>
void UnreadMessageReaction::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_big_);
  END_STORE_FLAGS();
  td::store(reaction_, storer);
  td::store(sender_dialog_id_, storer);
}

template <class ParserT>
void UnreadMessageReaction::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_big_);
  END_PARSE_FLAGS();
  td::parse(reaction_, parser);
  td::parse(sender_dialog_id_, parser);
}

template <class StorerT>
void MessageReactions::store(StorerT &storer) const {
  bool has_reactions = !reactions_.empty();
  bool has_unread_reactions = !unread_reactions_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_min_);
  STORE_FLAG(need_polling_);
  STORE_FLAG(can_get_added_reactions_);
  STORE_FLAG(has_unread_reactions);
  STORE_FLAG(has_reactions);
  END_STORE_FLAGS();
  if (has_reactions) {
    td::store(reactions_, storer);
  }
  if (has_unread_reactions) {
    td::store(unread_reactions_, storer);
  }
}

template <class ParserT>
void MessageReactions::parse(ParserT &parser) {
  bool has_reactions;
  bool has_unread_reactions;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_min_);
  PARSE_FLAG(need_polling_);
  PARSE_FLAG(can_get_added_reactions_);
  PARSE_FLAG(has_unread_reactions);
  PARSE_FLAG(has_reactions);
  END_PARSE_FLAGS();
  if (has_reactions) {
    td::parse(reactions_, parser);
  }
  if (has_unread_reactions) {
    td::parse(unread_reactions_, parser);
  }
}

}  // namespace td
