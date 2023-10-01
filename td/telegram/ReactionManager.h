//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChatReactions.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/ReactionType.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

#include <utility>

namespace td {

class Td;

class ReactionManager final : public Actor {
 public:
  ReactionManager(Td *td, ActorShared<> parent);
  ReactionManager(const ReactionManager &) = delete;
  ReactionManager &operator=(const ReactionManager &) = delete;
  ReactionManager(ReactionManager &&) = delete;
  ReactionManager &operator=(ReactionManager &&) = delete;
  ~ReactionManager() final;

  void init();

  bool is_active_reaction(const ReactionType &reaction_type) const;

  void get_emoji_reaction(const string &emoji, Promise<td_api::object_ptr<td_api::emojiReaction>> &&promise);

  td_api::object_ptr<td_api::availableReactions> get_sorted_available_reactions(ChatReactions available_reactions,
                                                                                ChatReactions active_reactions,
                                                                                int32 row_size);

  td_api::object_ptr<td_api::availableReactions> get_available_reactions(int32 row_size);

  void add_recent_reaction(const ReactionType &reaction_type);

  void clear_recent_reactions(Promise<Unit> &&promise);

  void reload_reactions();

  void reload_recent_reactions();

  void reload_top_reactions();

  void on_get_available_reactions(tl_object_ptr<telegram_api::messages_AvailableReactions> &&available_reactions_ptr);

  void on_get_recent_reactions(tl_object_ptr<telegram_api::messages_Reactions> &&reactions_ptr);

  void on_get_top_reactions(tl_object_ptr<telegram_api::messages_Reactions> &&reactions_ptr);

  void set_default_reaction(ReactionType reaction_type, Promise<Unit> &&promise);

  void send_set_default_reaction_query();

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  static constexpr size_t MAX_RECENT_REACTIONS = 100;  // some reasonable value

  struct Reaction {
    ReactionType reaction_type_;
    string title_;
    bool is_active_ = false;
    bool is_premium_ = false;
    FileId static_icon_;
    FileId appear_animation_;
    FileId select_animation_;
    FileId activate_animation_;
    FileId effect_animation_;
    FileId around_animation_;
    FileId center_animation_;

    bool is_valid() const {
      return static_icon_.is_valid() && appear_animation_.is_valid() && select_animation_.is_valid() &&
             activate_animation_.is_valid() && effect_animation_.is_valid() && !reaction_type_.is_empty();
    }

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct Reactions {
    int32 hash_ = 0;
    bool are_being_reloaded_ = false;
    vector<Reaction> reactions_;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct ReactionList {
    int64 hash_ = 0;
    bool is_being_reloaded_ = false;
    vector<ReactionType> reaction_types_;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  td_api::object_ptr<td_api::emojiReaction> get_emoji_reaction_object(const string &emoji) const;

  void start_up() final;

  void tear_down() final;

  void save_active_reactions();

  void save_reactions();

  void save_recent_reactions();

  void save_top_reactions();

  void load_active_reactions();

  void load_reactions();

  void load_recent_reactions();

  void load_top_reactions();

  void update_active_reactions();

  td_api::object_ptr<td_api::updateActiveEmojiReactions> get_update_active_emoji_reactions_object() const;

  Td *td_;
  ActorShared<> parent_;

  bool is_inited_ = false;

  vector<std::pair<string, Promise<td_api::object_ptr<td_api::emojiReaction>>>> pending_get_emoji_reaction_queries_;

  Reactions reactions_;
  vector<ReactionType> active_reaction_types_;

  ReactionList recent_reactions_;
  ReactionList top_reactions_;

  bool are_reactions_loaded_from_database_ = false;
  bool are_recent_reactions_loaded_from_database_ = false;
  bool are_top_reactions_loaded_from_database_ = false;
};

}  // namespace td
