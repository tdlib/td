//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ReactionManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/ReactionManager.hpp"
#include "td/telegram/StickerFormat.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/logging.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Status.h"

namespace td {

class GetAvailableReactionsQuery final : public Td::ResultHandler {
 public:
  void send(int32 hash) {
    send_query(G()->net_query_creator().create(telegram_api::messages_getAvailableReactions(hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getAvailableReactions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetAvailableReactionsQuery: " << to_string(ptr);
    td_->reaction_manager_->on_get_available_reactions(std::move(ptr));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for GetAvailableReactionsQuery: " << status;
    td_->reaction_manager_->on_get_available_reactions(nullptr);
  }
};

class GetRecentReactionsQuery final : public Td::ResultHandler {
 public:
  void send(int32 limit, int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::messages_getRecentReactions(limit, hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getRecentReactions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetRecentReactionsQuery: " << to_string(ptr);
    td_->reaction_manager_->on_get_recent_reactions(std::move(ptr));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for GetRecentReactionsQuery: " << status;
    td_->reaction_manager_->on_get_recent_reactions(nullptr);
  }
};

class GetTopReactionsQuery final : public Td::ResultHandler {
 public:
  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::messages_getTopReactions(50, hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getTopReactions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetTopReactionsQuery: " << to_string(ptr);
    td_->reaction_manager_->on_get_top_reactions(std::move(ptr));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for GetTopReactionsQuery: " << status;
    td_->reaction_manager_->on_get_top_reactions(nullptr);
  }
};

class ClearRecentReactionsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ClearRecentReactionsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::messages_clearRecentReactions()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_clearRecentReactions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->reaction_manager_->reload_recent_reactions();
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for clear recent reactions: " << status;
    }
    td_->reaction_manager_->reload_recent_reactions();
    promise_.set_error(std::move(status));
  }
};

class SetDefaultReactionQuery final : public Td::ResultHandler {
  ReactionType reaction_type_;

 public:
  void send(const ReactionType &reaction_type) {
    reaction_type_ = reaction_type;
    send_query(
        G()->net_query_creator().create(telegram_api::messages_setDefaultReaction(reaction_type.get_input_reaction())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_setDefaultReaction>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    if (!result_ptr.ok()) {
      return on_error(Status::Error(400, "Receive false"));
    }

    auto default_reaction = td_->option_manager_->get_option_string("default_reaction", "-");
    if (default_reaction != reaction_type_.get_string()) {
      td_->reaction_manager_->send_set_default_reaction_query();
    } else {
      td_->option_manager_->set_option_empty("default_reaction_needs_sync");
    }
  }

  void on_error(Status status) final {
    if (G()->close_flag()) {
      return;
    }

    LOG(INFO) << "Receive error for SetDefaultReactionQuery: " << status;
    td_->option_manager_->set_option_empty("default_reaction_needs_sync");
    send_closure(G()->config_manager(), &ConfigManager::request_config, false);
  }
};

ReactionManager::ReactionManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

ReactionManager::~ReactionManager() = default;

void ReactionManager::start_up() {
  init();
}

void ReactionManager::tear_down() {
  parent_.reset();
}

void ReactionManager::init() {
  if (G()->close_flag()) {
    return;
  }
  if (is_inited_ || !td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }
  is_inited_ = true;

  td_->stickers_manager_->init();

  load_active_reactions();

  if (td_->option_manager_->get_option_boolean("default_reaction_needs_sync")) {
    send_set_default_reaction_query();
  }
}

td_api::object_ptr<td_api::emojiReaction> ReactionManager::get_emoji_reaction_object(const string &emoji) const {
  for (auto &reaction : reactions_.reactions_) {
    if (reaction.reaction_type_.get_string() == emoji) {
      return td_api::make_object<td_api::emojiReaction>(
          reaction.reaction_type_.get_string(), reaction.title_, reaction.is_active_,
          td_->stickers_manager_->get_sticker_object(reaction.static_icon_),
          td_->stickers_manager_->get_sticker_object(reaction.appear_animation_),
          td_->stickers_manager_->get_sticker_object(reaction.select_animation_),
          td_->stickers_manager_->get_sticker_object(reaction.activate_animation_),
          td_->stickers_manager_->get_sticker_object(reaction.effect_animation_),
          td_->stickers_manager_->get_sticker_object(reaction.around_animation_),
          td_->stickers_manager_->get_sticker_object(reaction.center_animation_));
    }
  }
  return nullptr;
}

void ReactionManager::get_emoji_reaction(const string &emoji,
                                         Promise<td_api::object_ptr<td_api::emojiReaction>> &&promise) {
  load_reactions();
  if (reactions_.reactions_.empty() && reactions_.are_being_reloaded_) {
    pending_get_emoji_reaction_queries_.emplace_back(emoji, std::move(promise));
    return;
  }
  promise.set_value(get_emoji_reaction_object(emoji));
}

td_api::object_ptr<td_api::availableReactions> ReactionManager::get_sorted_available_reactions(
    ChatReactions available_reactions, ChatReactions active_reactions, int32 row_size) {
  load_recent_reactions();
  load_top_reactions();

  if (row_size < 5 || row_size > 25) {
    row_size = 8;
  }

  bool is_premium = td_->option_manager_->get_option_boolean("is_premium");
  bool show_premium = is_premium;
  const auto &recent_reactions = recent_reactions_.reaction_types_;
  auto top_reactions = top_reactions_.reaction_types_;
  LOG(INFO) << "Have available reactions " << available_reactions << " to be sorted by top reactions " << top_reactions
            << " and recent reactions " << recent_reactions;
  if (active_reactions.allow_custom_ && active_reactions.allow_all_) {
    for (auto &reaction_type : recent_reactions) {
      if (reaction_type.is_custom_reaction()) {
        show_premium = true;
      }
    }
    for (auto &reaction_type : top_reactions) {
      if (reaction_type.is_custom_reaction()) {
        show_premium = true;
      }
    }
  }

  FlatHashSet<ReactionType, ReactionTypeHash> all_available_reaction_types;
  for (const auto &reaction_type : available_reactions.reaction_types_) {
    CHECK(!reaction_type.is_empty());
    all_available_reaction_types.insert(reaction_type);
  }

  vector<td_api::object_ptr<td_api::availableReaction>> top_reaction_objects;
  vector<td_api::object_ptr<td_api::availableReaction>> recent_reaction_objects;
  vector<td_api::object_ptr<td_api::availableReaction>> popular_reaction_objects;
  vector<td_api::object_ptr<td_api::availableReaction>> last_reaction_objects;

  FlatHashSet<ReactionType, ReactionTypeHash> added_custom_reaction_types;
  auto add_reactions = [&](vector<td_api::object_ptr<td_api::availableReaction>> &reaction_objects,
                           const vector<ReactionType> &reaction_types) {
    for (auto &reaction_type : reaction_types) {
      if (all_available_reaction_types.erase(reaction_type) != 0) {
        // add available reaction
        if (reaction_type.is_custom_reaction()) {
          added_custom_reaction_types.insert(reaction_type);
        }
        reaction_objects.push_back(
            td_api::make_object<td_api::availableReaction>(reaction_type.get_reaction_type_object(), false));
      } else if (reaction_type.is_custom_reaction() && available_reactions.allow_custom_ &&
                 added_custom_reaction_types.insert(reaction_type).second) {
        // add implicitly available custom reaction
        reaction_objects.push_back(
            td_api::make_object<td_api::availableReaction>(reaction_type.get_reaction_type_object(), !is_premium));
      } else {
        // skip the reaction
      }
    }
  };
  if (show_premium) {
    if (top_reactions.size() > 2 * static_cast<size_t>(row_size)) {
      top_reactions.resize(2 * static_cast<size_t>(row_size));
    }
    add_reactions(top_reaction_objects, top_reactions);

    if (!recent_reactions.empty()) {
      add_reactions(recent_reaction_objects, recent_reactions);
    }
  } else {
    add_reactions(top_reaction_objects, top_reactions);
  }
  add_reactions(last_reaction_objects, active_reaction_types_);
  add_reactions(last_reaction_objects, available_reactions.reaction_types_);

  if (show_premium) {
    if (recent_reactions.empty()) {
      popular_reaction_objects = std::move(last_reaction_objects);
    } else {
      auto max_objects = 10 * static_cast<size_t>(row_size);
      if (recent_reaction_objects.size() + last_reaction_objects.size() > max_objects) {
        if (last_reaction_objects.size() < max_objects) {
          recent_reaction_objects.resize(max_objects - last_reaction_objects.size());
        } else {
          recent_reaction_objects.clear();
        }
      }
      append(recent_reaction_objects, std::move(last_reaction_objects));
    }
  } else {
    append(top_reaction_objects, std::move(last_reaction_objects));
  }

  CHECK(all_available_reaction_types.empty());

  return td_api::make_object<td_api::availableReactions>(
      std::move(top_reaction_objects), std::move(recent_reaction_objects), std::move(popular_reaction_objects),
      available_reactions.allow_custom_);
}

td_api::object_ptr<td_api::availableReactions> ReactionManager::get_available_reactions(int32 row_size) {
  ChatReactions available_reactions;
  available_reactions.reaction_types_ = active_reaction_types_;
  available_reactions.allow_custom_ = true;
  return get_sorted_available_reactions(std::move(available_reactions), ChatReactions(true, true), row_size);
}

void ReactionManager::add_recent_reaction(const ReactionType &reaction_type) {
  load_recent_reactions();

  auto &reactions = recent_reactions_.reaction_types_;
  if (!reactions.empty() && reactions[0] == reaction_type) {
    return;
  }

  add_to_top(reactions, MAX_RECENT_REACTIONS, reaction_type);

  recent_reactions_.hash_ = get_reaction_types_hash(reactions);
}

void ReactionManager::clear_recent_reactions(Promise<Unit> &&promise) {
  load_recent_reactions();

  if (recent_reactions_.reaction_types_.empty()) {
    return promise.set_value(Unit());
  }

  recent_reactions_.hash_ = 0;
  recent_reactions_.reaction_types_.clear();

  td_->create_handler<ClearRecentReactionsQuery>(std::move(promise))->send();
}

void ReactionManager::reload_reactions() {
  if (G()->close_flag() || reactions_.are_being_reloaded_) {
    return;
  }
  CHECK(!td_->auth_manager_->is_bot());
  reactions_.are_being_reloaded_ = true;
  load_reactions();  // must be after are_being_reloaded_ is set to true to avoid recursion
  td_->create_handler<GetAvailableReactionsQuery>()->send(reactions_.hash_);
}

void ReactionManager::reload_recent_reactions() {
  if (G()->close_flag() || recent_reactions_.is_being_reloaded_) {
    return;
  }
  CHECK(!td_->auth_manager_->is_bot());
  recent_reactions_.is_being_reloaded_ = true;
  load_recent_reactions();  // must be after is_being_reloaded_ is set to true to avoid recursion
  td_->create_handler<GetRecentReactionsQuery>()->send(MAX_RECENT_REACTIONS, recent_reactions_.hash_);
}

void ReactionManager::reload_top_reactions() {
  if (G()->close_flag() || top_reactions_.is_being_reloaded_) {
    return;
  }
  CHECK(!td_->auth_manager_->is_bot());
  top_reactions_.is_being_reloaded_ = true;
  load_top_reactions();  // must be after is_being_reloaded_ is set to true to avoid recursion
  td_->create_handler<GetTopReactionsQuery>()->send(top_reactions_.hash_);
}

td_api::object_ptr<td_api::updateActiveEmojiReactions> ReactionManager::get_update_active_emoji_reactions_object()
    const {
  return td_api::make_object<td_api::updateActiveEmojiReactions>(
      transform(active_reaction_types_, [](const ReactionType &reaction_type) { return reaction_type.get_string(); }));
}

void ReactionManager::save_active_reactions() {
  LOG(INFO) << "Save " << active_reaction_types_.size() << " active reactions";
  G()->td_db()->get_binlog_pmc()->set("active_reactions", log_event_store(active_reaction_types_).as_slice().str());
}

void ReactionManager::save_reactions() {
  LOG(INFO) << "Save " << reactions_.reactions_.size() << " available reactions";
  are_reactions_loaded_from_database_ = true;
  G()->td_db()->get_binlog_pmc()->set("reactions", log_event_store(reactions_).as_slice().str());
}

void ReactionManager::save_recent_reactions() {
  LOG(INFO) << "Save " << recent_reactions_.reaction_types_.size() << " recent reactions";
  are_recent_reactions_loaded_from_database_ = true;
  G()->td_db()->get_binlog_pmc()->set("recent_reactions", log_event_store(recent_reactions_).as_slice().str());
}

void ReactionManager::save_top_reactions() {
  LOG(INFO) << "Save " << top_reactions_.reaction_types_.size() << " top reactions";
  are_top_reactions_loaded_from_database_ = true;
  G()->td_db()->get_binlog_pmc()->set("top_reactions", log_event_store(top_reactions_).as_slice().str());
}

void ReactionManager::load_active_reactions() {
  LOG(INFO) << "Loading active reactions";
  string active_reaction_types = G()->td_db()->get_binlog_pmc()->get("active_reactions");
  if (active_reaction_types.empty()) {
    return reload_reactions();
  }

  auto status = log_event_parse(active_reaction_types_, active_reaction_types);
  if (status.is_error()) {
    LOG(ERROR) << "Can't load active reactions: " << status;
    active_reaction_types_ = {};
    return reload_reactions();
  }

  LOG(INFO) << "Successfully loaded " << active_reaction_types_.size() << " active reactions";

  td_->messages_manager_->set_active_reactions(active_reaction_types_);

  send_closure(G()->td(), &Td::send_update, get_update_active_emoji_reactions_object());
}

void ReactionManager::load_reactions() {
  if (are_reactions_loaded_from_database_) {
    return;
  }
  are_reactions_loaded_from_database_ = true;

  LOG(INFO) << "Loading available reactions";
  string reactions = G()->td_db()->get_binlog_pmc()->get("reactions");
  if (reactions.empty()) {
    return reload_reactions();
  }

  auto new_reactions = reactions_;
  auto status = log_event_parse(new_reactions, reactions);
  if (status.is_error()) {
    LOG(ERROR) << "Can't load available reactions: " << status;
    return reload_reactions();
  }
  for (auto &reaction_type : new_reactions.reactions_) {
    if (!reaction_type.is_valid()) {
      LOG(ERROR) << "Loaded invalid reaction";
      return reload_reactions();
    }
  }
  reactions_ = std::move(new_reactions);

  LOG(INFO) << "Successfully loaded " << reactions_.reactions_.size() << " available reactions";

  update_active_reactions();
}

void ReactionManager::load_recent_reactions() {
  if (are_recent_reactions_loaded_from_database_) {
    return;
  }
  are_recent_reactions_loaded_from_database_ = true;

  LOG(INFO) << "Loading recent reactions";
  string recent_reactions = G()->td_db()->get_binlog_pmc()->get("recent_reactions");
  if (recent_reactions.empty()) {
    return reload_recent_reactions();
  }

  auto status = log_event_parse(recent_reactions_, recent_reactions);
  if (status.is_error()) {
    LOG(ERROR) << "Can't load recent reactions: " << status;
    recent_reactions_ = {};
    return reload_recent_reactions();
  }

  LOG(INFO) << "Successfully loaded " << recent_reactions_.reaction_types_.size() << " recent reactions";
}

void ReactionManager::load_top_reactions() {
  if (are_top_reactions_loaded_from_database_) {
    return;
  }
  are_top_reactions_loaded_from_database_ = true;

  LOG(INFO) << "Loading top reactions";
  string top_reactions = G()->td_db()->get_binlog_pmc()->get("top_reactions");
  if (top_reactions.empty()) {
    return reload_top_reactions();
  }

  auto status = log_event_parse(top_reactions_, top_reactions);
  if (status.is_error()) {
    LOG(ERROR) << "Can't load top reactions: " << status;
    top_reactions_ = {};
    return reload_top_reactions();
  }

  LOG(INFO) << "Successfully loaded " << top_reactions_.reaction_types_.size() << " top reactions";
}

void ReactionManager::update_active_reactions() {
  vector<ReactionType> active_reaction_types;
  for (auto &reaction : reactions_.reactions_) {
    if (reaction.is_active_) {
      active_reaction_types.emplace_back(reaction.reaction_type_);
    }
  }
  if (active_reaction_types == active_reaction_types_) {
    return;
  }
  active_reaction_types_ = active_reaction_types;

  save_active_reactions();

  send_closure(G()->td(), &Td::send_update, get_update_active_emoji_reactions_object());

  td_->messages_manager_->set_active_reactions(std::move(active_reaction_types));
}

void ReactionManager::on_get_available_reactions(
    tl_object_ptr<telegram_api::messages_AvailableReactions> &&available_reactions_ptr) {
  CHECK(reactions_.are_being_reloaded_);
  reactions_.are_being_reloaded_ = false;

  auto get_emoji_reaction_queries = std::move(pending_get_emoji_reaction_queries_);
  pending_get_emoji_reaction_queries_.clear();
  SCOPE_EXIT {
    for (auto &query : get_emoji_reaction_queries) {
      query.second.set_value(get_emoji_reaction_object(query.first));
    }
  };

  if (available_reactions_ptr == nullptr) {
    // failed to get available reactions
    return;
  }

  int32 constructor_id = available_reactions_ptr->get_id();
  if (constructor_id == telegram_api::messages_availableReactionsNotModified::ID) {
    LOG(INFO) << "Available reactions are not modified";
    return;
  }

  CHECK(constructor_id == telegram_api::messages_availableReactions::ID);
  auto available_reactions = move_tl_object_as<telegram_api::messages_availableReactions>(available_reactions_ptr);
  vector<Reaction> new_reactions;
  for (auto &available_reaction : available_reactions->reactions_) {
    Reaction reaction;
    reaction.is_active_ = !available_reaction->inactive_;
    reaction.is_premium_ = available_reaction->premium_;
    reaction.reaction_type_ = ReactionType(std::move(available_reaction->reaction_));
    reaction.title_ = std::move(available_reaction->title_);
    reaction.static_icon_ =
        td_->stickers_manager_
            ->on_get_sticker_document(std::move(available_reaction->static_icon_), StickerFormat::Webp)
            .second;
    reaction.appear_animation_ =
        td_->stickers_manager_
            ->on_get_sticker_document(std::move(available_reaction->appear_animation_), StickerFormat::Tgs)
            .second;
    reaction.select_animation_ =
        td_->stickers_manager_
            ->on_get_sticker_document(std::move(available_reaction->select_animation_), StickerFormat::Tgs)
            .second;
    reaction.activate_animation_ =
        td_->stickers_manager_
            ->on_get_sticker_document(std::move(available_reaction->activate_animation_), StickerFormat::Tgs)
            .second;
    reaction.effect_animation_ =
        td_->stickers_manager_
            ->on_get_sticker_document(std::move(available_reaction->effect_animation_), StickerFormat::Tgs)
            .second;
    reaction.around_animation_ =
        td_->stickers_manager_
            ->on_get_sticker_document(std::move(available_reaction->around_animation_), StickerFormat::Tgs)
            .second;
    reaction.center_animation_ =
        td_->stickers_manager_->on_get_sticker_document(std::move(available_reaction->center_icon_), StickerFormat::Tgs)
            .second;

    if (!reaction.is_valid()) {
      LOG(ERROR) << "Receive invalid " << reaction.reaction_type_;
      continue;
    }
    if (reaction.is_premium_) {
      LOG(ERROR) << "Receive premium " << reaction.reaction_type_;
      continue;
    }

    new_reactions.push_back(std::move(reaction));
  }
  reactions_.reactions_ = std::move(new_reactions);
  reactions_.hash_ = available_reactions->hash_;

  save_reactions();

  update_active_reactions();
}

void ReactionManager::on_get_recent_reactions(tl_object_ptr<telegram_api::messages_Reactions> &&reactions_ptr) {
  CHECK(recent_reactions_.is_being_reloaded_);
  recent_reactions_.is_being_reloaded_ = false;

  if (reactions_ptr == nullptr) {
    // failed to get recent reactions
    return;
  }

  int32 constructor_id = reactions_ptr->get_id();
  if (constructor_id == telegram_api::messages_reactionsNotModified::ID) {
    LOG(INFO) << "Top reactions are not modified";
    return;
  }

  CHECK(constructor_id == telegram_api::messages_reactions::ID);
  auto reactions = move_tl_object_as<telegram_api::messages_reactions>(reactions_ptr);
  auto new_reaction_types = transform(
      reactions->reactions_,
      [](const telegram_api::object_ptr<telegram_api::Reaction> &reaction) { return ReactionType(reaction); });
  if (new_reaction_types == recent_reactions_.reaction_types_ && recent_reactions_.hash_ == reactions->hash_) {
    LOG(INFO) << "Top reactions are not modified";
    return;
  }
  recent_reactions_.reaction_types_ = std::move(new_reaction_types);
  recent_reactions_.hash_ = reactions->hash_;

  auto expected_hash = get_reaction_types_hash(recent_reactions_.reaction_types_);
  if (recent_reactions_.hash_ != expected_hash) {
    LOG(ERROR) << "Receive hash " << recent_reactions_.hash_ << " instead of " << expected_hash << " for reactions "
               << recent_reactions_.reaction_types_;
  }

  save_recent_reactions();
}

void ReactionManager::on_get_top_reactions(tl_object_ptr<telegram_api::messages_Reactions> &&reactions_ptr) {
  CHECK(top_reactions_.is_being_reloaded_);
  top_reactions_.is_being_reloaded_ = false;

  if (reactions_ptr == nullptr) {
    // failed to get top reactions
    return;
  }

  int32 constructor_id = reactions_ptr->get_id();
  if (constructor_id == telegram_api::messages_reactionsNotModified::ID) {
    LOG(INFO) << "Top reactions are not modified";
    return;
  }

  CHECK(constructor_id == telegram_api::messages_reactions::ID);
  auto reactions = move_tl_object_as<telegram_api::messages_reactions>(reactions_ptr);
  auto new_reaction_types = transform(
      reactions->reactions_,
      [](const telegram_api::object_ptr<telegram_api::Reaction> &reaction) { return ReactionType(reaction); });
  if (new_reaction_types == top_reactions_.reaction_types_ && top_reactions_.hash_ == reactions->hash_) {
    LOG(INFO) << "Top reactions are not modified";
    return;
  }
  top_reactions_.reaction_types_ = std::move(new_reaction_types);
  top_reactions_.hash_ = reactions->hash_;

  save_top_reactions();
}

bool ReactionManager::is_active_reaction(const ReactionType &reaction_type) const {
  return td::contains(active_reaction_types_, reaction_type);
}

void ReactionManager::set_default_reaction(ReactionType reaction_type, Promise<Unit> &&promise) {
  if (reaction_type.is_empty()) {
    return promise.set_error(Status::Error(400, "Default reaction must be non-empty"));
  }
  if (!reaction_type.is_custom_reaction() && !is_active_reaction(reaction_type)) {
    return promise.set_error(Status::Error(400, "Can't set incative reaction as default"));
  }

  if (td_->option_manager_->get_option_string("default_reaction", "-") != reaction_type.get_string()) {
    td_->option_manager_->set_option_string("default_reaction", reaction_type.get_string());
    if (!td_->option_manager_->get_option_boolean("default_reaction_needs_sync")) {
      td_->option_manager_->set_option_boolean("default_reaction_needs_sync", true);
      send_set_default_reaction_query();
    }
  }
  promise.set_value(Unit());
}

void ReactionManager::send_set_default_reaction_query() {
  td_->create_handler<SetDefaultReactionQuery>()->send(
      ReactionType(td_->option_manager_->get_option_string("default_reaction")));
}

void ReactionManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (!active_reaction_types_.empty()) {
    updates.push_back(get_update_active_emoji_reactions_object());
  }
}

}  // namespace td
