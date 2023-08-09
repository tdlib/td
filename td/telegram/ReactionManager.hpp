//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ReactionManager.h"

#include "td/telegram/ReactionType.hpp"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StickersManager.hpp"
#include "td/telegram/Td.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void ReactionManager::Reaction::store(StorerT &storer) const {
  StickersManager *stickers_manager = storer.context()->td().get_actor_unsafe()->stickers_manager_.get();
  bool has_around_animation = !around_animation_.empty();
  bool has_center_animation = !center_animation_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_active_);
  STORE_FLAG(has_around_animation);
  STORE_FLAG(has_center_animation);
  STORE_FLAG(is_premium_);
  END_STORE_FLAGS();
  td::store(reaction_type_, storer);
  td::store(title_, storer);
  stickers_manager->store_sticker(static_icon_, false, storer, "Reaction");
  stickers_manager->store_sticker(appear_animation_, false, storer, "Reaction");
  stickers_manager->store_sticker(select_animation_, false, storer, "Reaction");
  stickers_manager->store_sticker(activate_animation_, false, storer, "Reaction");
  stickers_manager->store_sticker(effect_animation_, false, storer, "Reaction");
  if (has_around_animation) {
    stickers_manager->store_sticker(around_animation_, false, storer, "Reaction");
  }
  if (has_center_animation) {
    stickers_manager->store_sticker(center_animation_, false, storer, "Reaction");
  }
}

template <class ParserT>
void ReactionManager::Reaction::parse(ParserT &parser) {
  StickersManager *stickers_manager = parser.context()->td().get_actor_unsafe()->stickers_manager_.get();
  bool has_around_animation;
  bool has_center_animation;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_active_);
  PARSE_FLAG(has_around_animation);
  PARSE_FLAG(has_center_animation);
  PARSE_FLAG(is_premium_);
  END_PARSE_FLAGS();
  td::parse(reaction_type_, parser);
  td::parse(title_, parser);
  static_icon_ = stickers_manager->parse_sticker(false, parser);
  appear_animation_ = stickers_manager->parse_sticker(false, parser);
  select_animation_ = stickers_manager->parse_sticker(false, parser);
  activate_animation_ = stickers_manager->parse_sticker(false, parser);
  effect_animation_ = stickers_manager->parse_sticker(false, parser);
  if (has_around_animation) {
    around_animation_ = stickers_manager->parse_sticker(false, parser);
  }
  if (has_center_animation) {
    center_animation_ = stickers_manager->parse_sticker(false, parser);
  }

  is_premium_ = false;
}

template <class StorerT>
void ReactionManager::Reactions::store(StorerT &storer) const {
  bool has_reactions = !reactions_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_reactions);
  END_STORE_FLAGS();
  if (has_reactions) {
    td::store(reactions_, storer);
    td::store(hash_, storer);
  }
}

template <class ParserT>
void ReactionManager::Reactions::parse(ParserT &parser) {
  bool has_reactions;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_reactions);
  END_PARSE_FLAGS();
  if (has_reactions) {
    td::parse(reactions_, parser);
    td::parse(hash_, parser);
  }
}

template <class StorerT>
void ReactionManager::ReactionList::store(StorerT &storer) const {
  bool has_reaction_types = !reaction_types_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_reaction_types);
  END_STORE_FLAGS();
  if (has_reaction_types) {
    td::store(reaction_types_, storer);
    td::store(hash_, storer);
  }
}

template <class ParserT>
void ReactionManager::ReactionList::parse(ParserT &parser) {
  bool has_reaction_types;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_reaction_types);
  END_PARSE_FLAGS();
  if (has_reaction_types) {
    td::parse(reaction_types_, parser);
    td::parse(hash_, parser);
  }
}

}  // namespace td
