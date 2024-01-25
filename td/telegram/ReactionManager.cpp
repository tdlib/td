//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ReactionManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/ReactionManager.hpp"
#include "td/telegram/StickerFormat.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/logging.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Status.h"

#include <algorithm>
#include <type_traits>

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

class GetReactionListQuery final : public Td::ResultHandler {
  ReactionListType reaction_list_type_;

 public:
  void send(ReactionListType reaction_list_type, int64 hash) {
    reaction_list_type_ = reaction_list_type;
    switch (reaction_list_type) {
      case ReactionListType::Recent:
        send_query(G()->net_query_creator().create(
            telegram_api::messages_getRecentReactions(ReactionManager::MAX_RECENT_REACTIONS, hash)));
        break;
      case ReactionListType::Top:
        send_query(G()->net_query_creator().create(telegram_api::messages_getTopReactions(200, hash)));
        break;
      case ReactionListType::DefaultTag:
        send_query(G()->net_query_creator().create(telegram_api::messages_getDefaultTagReactions(hash)));
        break;
      default:
        UNREACHABLE();
        break;
    }
  }

  void on_result(BufferSlice packet) final {
    static_assert(std::is_same<telegram_api::messages_getRecentReactions::ReturnType,
                               telegram_api::messages_getTopReactions::ReturnType>::value,
                  "");
    static_assert(std::is_same<telegram_api::messages_getRecentReactions::ReturnType,
                               telegram_api::messages_getDefaultTagReactions::ReturnType>::value,
                  "");
    auto result_ptr = fetch_result<telegram_api::messages_getRecentReactions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetReactionListQuery: " << to_string(ptr);
    td_->reaction_manager_->on_get_reaction_list(reaction_list_type_, std::move(ptr));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for GetReactionListQuery: " << status;
    td_->reaction_manager_->on_get_reaction_list(reaction_list_type_, nullptr);
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

    td_->reaction_manager_->reload_reaction_list(ReactionListType::Recent);
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for clear recent reactions: " << status;
    }
    td_->reaction_manager_->reload_reaction_list(ReactionListType::Recent);
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

class GetSavedReactionTagsQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::messages_SavedReactionTags>> promise_;

 public:
  explicit GetSavedReactionTagsQuery(
      Promise<telegram_api::object_ptr<telegram_api::messages_SavedReactionTags>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::messages_getSavedReactionTags(hash),
                                               {td_->dialog_manager_->get_my_dialog_id()}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getSavedReactionTags>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetSavedReactionTagsQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class UpdateSavedReactionTagQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit UpdateSavedReactionTagQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const ReactionType &reaction_type, const string &title) {
    int32 flags = 0;
    if (!title.empty()) {
      flags |= telegram_api::messages_updateSavedReactionTag::TITLE_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_updateSavedReactionTag(flags, reaction_type.get_input_reaction(), title)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_updateSavedReactionTag>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

ReactionManager::SavedReactionTag::SavedReactionTag(telegram_api::object_ptr<telegram_api::savedReactionTag> &&tag)
    : reaction_type_(tag->reaction_)
    , hash_(reaction_type_.get_hash())
    , title_(std::move(tag->title_))
    , count_(tag->count_) {
}

ReactionManager::SavedReactionTag::SavedReactionTag(const ReactionType &reaction_type, const string &title, int32 count)
    : reaction_type_(reaction_type), hash_(reaction_type_.get_hash()), title_(title), count_(count) {
}

td_api::object_ptr<td_api::savedMessagesTag> ReactionManager::SavedReactionTag::get_saved_messages_tag_object() const {
  return td_api::make_object<td_api::savedMessagesTag>(reaction_type_.get_reaction_type_object(), title_, count_);
}

bool operator==(const ReactionManager::SavedReactionTag &lhs, const ReactionManager::SavedReactionTag &rhs) {
  return lhs.reaction_type_ == rhs.reaction_type_ && lhs.title_ == rhs.title_ && lhs.count_ == rhs.count_;
}

bool operator!=(const ReactionManager::SavedReactionTag &lhs, const ReactionManager::SavedReactionTag &rhs) {
  return !(lhs == rhs);
}

bool operator<(const ReactionManager::SavedReactionTag &lhs, const ReactionManager::SavedReactionTag &rhs) {
  if (lhs.count_ != rhs.count_) {
    return lhs.count_ > rhs.count_;
  }
  return lhs.hash_ > rhs.hash_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const ReactionManager::SavedReactionTag &saved_reaction_tag) {
  return string_builder << "SavedMessagesTag{" << saved_reaction_tag.reaction_type_ << '(' << saved_reaction_tag.title_
                        << ") X " << saved_reaction_tag.count_ << '}';
}

td_api::object_ptr<td_api::savedMessagesTags> ReactionManager::SavedReactionTags::get_saved_messages_tags_object()
    const {
  return td_api::make_object<td_api::savedMessagesTags>(
      transform(tags_, [](const SavedReactionTag &tag) { return tag.get_saved_messages_tag_object(); }));
}

void ReactionManager::SavedReactionTags::update_saved_messages_tags(const vector<ReactionType> &old_tags,
                                                                    const vector<ReactionType> &new_tags,
                                                                    bool &is_changed, bool &need_reload_title) {
  if (!is_inited_) {
    return;
  }
  is_changed = false;
  for (const auto &old_tag : old_tags) {
    if (!td::contains(new_tags, old_tag)) {
      CHECK(!old_tag.is_empty());
      for (auto it = tags_.begin(); it != tags_.end(); ++it) {
        auto &tag = *it;
        if (tag.reaction_type_ == old_tag) {
          tag.count_--;
          if (!tag.is_valid()) {
            tags_.erase(it);
          }
          is_changed = true;
          break;
        }
      }
    }
  }
  for (const auto &new_tag : new_tags) {
    if (!td::contains(old_tags, new_tag)) {
      CHECK(!new_tag.is_empty());
      is_changed = true;
      bool is_found = false;
      for (auto &tag : tags_) {
        if (tag.reaction_type_ == new_tag) {
          tag.count_++;
          is_found = true;
          break;
        }
      }
      if (!is_found) {
        tags_.emplace_back(new_tag, string(), 1);
        need_reload_title = true;
      }
    }
  }
  if (is_changed) {
    std::sort(tags_.begin(), tags_.end());
    hash_ = calc_hash();
  }
}

bool ReactionManager::SavedReactionTags::set_tag_title(const ReactionType &reaction_type, const string &title) {
  if (!is_inited_) {
    return false;
  }
  for (auto it = tags_.begin(); it != tags_.end(); ++it) {
    auto &tag = *it;
    if (tag.reaction_type_ == reaction_type) {
      if (tag.title_ == title) {
        return false;
      }
      tag.title_ = title;
      if (!tag.is_valid()) {
        tags_.erase(it);
      }
      hash_ = calc_hash();
      return true;
    }
  }
  tags_.emplace_back(reaction_type, title, 0);
  std::sort(tags_.begin(), tags_.end());
  hash_ = calc_hash();
  return false;
}

int64 ReactionManager::SavedReactionTags::calc_hash() const {
  vector<uint64> numbers;
  for (const auto &tag : tags_) {
    numbers.push_back(tag.hash_);
    if (!tag.title_.empty()) {
      numbers.push_back(get_md5_string_hash(tag.title_));
    }
    numbers.push_back(tag.count_);
  }
  return get_vector_hash(numbers);
}

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
    ChatReactions available_reactions, ChatReactions active_reactions, int32 row_size, bool is_tag,
    ReactionUnavailabilityReason unavailability_reason) {
  if (is_tag) {
    load_reaction_list(ReactionListType::DefaultTag);
  } else {
    load_reaction_list(ReactionListType::Recent);
  }
  load_reaction_list(ReactionListType::Top);

  if (row_size < 5 || row_size > 25) {
    row_size = 8;
  }

  bool is_premium = td_->option_manager_->get_option_boolean("is_premium");
  bool show_premium = is_premium;
  vector<ReactionType> recent_reactions;
  vector<ReactionType> top_reactions;
  if (is_tag) {
    top_reactions = get_reaction_list(ReactionListType::DefaultTag).reaction_types_;
    if (is_premium) {
      append(top_reactions, get_reaction_list(ReactionListType::Top).reaction_types_);
    }
  } else {
    recent_reactions = get_reaction_list(ReactionListType::Recent).reaction_types_;
    top_reactions = get_reaction_list(ReactionListType::Top).reaction_types_;
  }
  LOG(INFO) << "Have available reactions " << available_reactions << " to be sorted by top reactions " << top_reactions
            << " and recent reactions " << recent_reactions;
  if (active_reactions.allow_all_custom_ && active_reactions.allow_all_regular_) {
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
      } else if (reaction_type.is_custom_reaction() && available_reactions.allow_all_custom_ &&
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

  td_api::object_ptr<td_api::ReactionUnavailabilityReason> reason;
  switch (unavailability_reason) {
    case ReactionUnavailabilityReason::None:
      break;
    case ReactionUnavailabilityReason::AnonymousAdministrator:
      reason = td_api::make_object<td_api::reactionUnavailabilityReasonAnonymousAdministrator>();
      break;
    case ReactionUnavailabilityReason::Guest:
      reason = td_api::make_object<td_api::reactionUnavailabilityReasonGuest>();
      break;
    default:
      UNREACHABLE();
  }

  return td_api::make_object<td_api::availableReactions>(
      std::move(top_reaction_objects), std::move(recent_reaction_objects), std::move(popular_reaction_objects),
      available_reactions.allow_all_custom_, is_tag, std::move(reason));
}

td_api::object_ptr<td_api::availableReactions> ReactionManager::get_available_reactions(int32 row_size) {
  ChatReactions available_reactions;
  available_reactions.reaction_types_ = active_reaction_types_;
  available_reactions.allow_all_custom_ = true;
  return get_sorted_available_reactions(std::move(available_reactions), ChatReactions(true, true), row_size, false,
                                        ReactionUnavailabilityReason::None);
}

void ReactionManager::add_recent_reaction(const ReactionType &reaction_type) {
  load_reaction_list(ReactionListType::Recent);

  auto &recent_reactions = get_reaction_list(ReactionListType::Recent);
  auto &reactions = recent_reactions.reaction_types_;
  if (!reactions.empty() && reactions[0] == reaction_type) {
    return;
  }

  add_to_top(reactions, MAX_RECENT_REACTIONS, reaction_type);

  recent_reactions.hash_ = get_reaction_types_hash(reactions);
}

void ReactionManager::clear_recent_reactions(Promise<Unit> &&promise) {
  load_reaction_list(ReactionListType::Recent);

  auto &recent_reactions = get_reaction_list(ReactionListType::Recent);
  if (recent_reactions.reaction_types_.empty()) {
    return promise.set_value(Unit());
  }

  recent_reactions.hash_ = 0;
  recent_reactions.reaction_types_.clear();

  td_->create_handler<ClearRecentReactionsQuery>(std::move(promise))->send();
}

vector<ReactionType> ReactionManager::get_default_tag_reactions() {
  load_reaction_list(ReactionListType::DefaultTag);

  return get_reaction_list(ReactionListType::DefaultTag).reaction_types_;
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

void ReactionManager::reload_reaction_list(ReactionListType reaction_list_type) {
  if (G()->close_flag()) {
    return;
  }
  auto &reaction_list = get_reaction_list(reaction_list_type);
  if (reaction_list.is_being_reloaded_) {
    return;
  }
  CHECK(!td_->auth_manager_->is_bot());
  reaction_list.is_being_reloaded_ = true;
  load_reaction_list(reaction_list_type);  // must be after is_being_reloaded_ is set to true to avoid recursion
  td_->create_handler<GetReactionListQuery>()->send(reaction_list_type, reaction_list.hash_);
}

td_api::object_ptr<td_api::updateActiveEmojiReactions> ReactionManager::get_update_active_emoji_reactions_object()
    const {
  return td_api::make_object<td_api::updateActiveEmojiReactions>(
      transform(active_reaction_types_, [](const ReactionType &reaction_type) { return reaction_type.get_string(); }));
}

ReactionManager::ReactionList &ReactionManager::get_reaction_list(ReactionListType reaction_list_type) {
  return reaction_lists_[static_cast<int32>(reaction_list_type)];
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

void ReactionManager::save_reaction_list(ReactionListType reaction_list_type) {
  auto &reaction_list = get_reaction_list(reaction_list_type);
  LOG(INFO) << "Save " << reaction_list.reaction_types_.size() << ' ' << reaction_list_type;
  reaction_list.is_loaded_from_database_ = true;
  G()->td_db()->get_binlog_pmc()->set(get_reaction_list_type_database_key(reaction_list_type),
                                      log_event_store(reaction_list).as_slice().str());
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

void ReactionManager::load_reaction_list(ReactionListType reaction_list_type) {
  auto &reaction_list = get_reaction_list(reaction_list_type);
  if (reaction_list.is_loaded_from_database_) {
    return;
  }
  reaction_list.is_loaded_from_database_ = true;

  LOG(INFO) << "Loading " << reaction_list_type;
  string reactions_str = G()->td_db()->get_binlog_pmc()->get(get_reaction_list_type_database_key(reaction_list_type));
  if (reactions_str.empty()) {
    return reload_reaction_list(reaction_list_type);
  }

  auto status = log_event_parse(reaction_list, reactions_str);
  if (status.is_error()) {
    LOG(ERROR) << "Can't load " << reaction_list_type << ": " << status;
    reaction_list = {};
    return reload_reaction_list(reaction_list_type);
  }

  LOG(INFO) << "Successfully loaded " << reaction_list.reaction_types_.size() << ' ' << reaction_list_type;
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

void ReactionManager::on_get_reaction_list(ReactionListType reaction_list_type,
                                           tl_object_ptr<telegram_api::messages_Reactions> &&reactions_ptr) {
  auto &reaction_list = get_reaction_list(reaction_list_type);
  CHECK(reaction_list.is_being_reloaded_);
  reaction_list.is_being_reloaded_ = false;

  if (reactions_ptr == nullptr) {
    // failed to get reactions
    return;
  }

  int32 constructor_id = reactions_ptr->get_id();
  if (constructor_id == telegram_api::messages_reactionsNotModified::ID) {
    LOG(INFO) << "List of " << reaction_list_type << " is not modified";
    return;
  }

  CHECK(constructor_id == telegram_api::messages_reactions::ID);
  auto reactions = move_tl_object_as<telegram_api::messages_reactions>(reactions_ptr);
  auto new_reaction_types = ReactionType::get_reaction_types(reactions->reactions_);
  if (new_reaction_types == reaction_list.reaction_types_ && reaction_list.hash_ == reactions->hash_) {
    LOG(INFO) << "List of " << reaction_list_type << " is not modified";
    return;
  }
  reaction_list.reaction_types_ = std::move(new_reaction_types);
  reaction_list.hash_ = reactions->hash_;

  auto expected_hash = get_reaction_types_hash(reaction_list.reaction_types_);
  if (reaction_list.hash_ != expected_hash) {
    LOG(ERROR) << "Receive hash " << reaction_list.hash_ << " instead of " << expected_hash << " for "
               << reaction_list_type << reaction_list.reaction_types_;
  }

  save_reaction_list(reaction_list_type);
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

void ReactionManager::get_saved_messages_tags(Promise<td_api::object_ptr<td_api::savedMessagesTags>> &&promise) {
  if (tags_.is_inited_) {
    return promise.set_value(tags_.get_saved_messages_tags_object());
  }
  reget_saved_messages_tags(std::move(promise));
}

void ReactionManager::reget_saved_messages_tags(Promise<td_api::object_ptr<td_api::savedMessagesTags>> &&promise) {
  auto &promises = pending_get_saved_reaction_tags_queries_;
  promises.push_back(std::move(promise));
  if (promises.size() != 1) {
    return;
  }
  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::messages_SavedReactionTags>> r_tags) {
        send_closure(actor_id, &ReactionManager::on_get_saved_messages_tags, std::move(r_tags));
      });
  td_->create_handler<GetSavedReactionTagsQuery>(std::move(query_promise))->send(tags_.hash_);
}

void ReactionManager::on_get_saved_messages_tags(
    Result<telegram_api::object_ptr<telegram_api::messages_SavedReactionTags>> &&r_tags) {
  G()->ignore_result_if_closing(r_tags);
  auto promises = std::move(pending_get_saved_reaction_tags_queries_);
  reset_to_empty(pending_get_saved_reaction_tags_queries_);
  CHECK(!promises.empty());

  if (r_tags.is_error()) {
    return fail_promises(promises, r_tags.move_as_error());
  }

  auto tags_ptr = r_tags.move_as_ok();
  bool need_send_update = false;
  switch (tags_ptr->get_id()) {
    case telegram_api::messages_savedReactionTagsNotModified::ID:
      // nothing to do
      break;
    case telegram_api::messages_savedReactionTags::ID: {
      auto tags = telegram_api::move_object_as<telegram_api::messages_savedReactionTags>(tags_ptr);
      vector<SavedReactionTag> saved_reaction_tags;
      for (auto &tag : tags->tags_) {
        saved_reaction_tags.emplace_back(std::move(tag));
        if (!saved_reaction_tags.back().is_valid()) {
          LOG(ERROR) << "Receive " << saved_reaction_tags.back();
          saved_reaction_tags.pop_back();
        }
      }
      std::sort(saved_reaction_tags.begin(), saved_reaction_tags.end());
      tags_.hash_ = tags->hash_;
      if (saved_reaction_tags != tags_.tags_) {
        tags_.tags_ = std::move(saved_reaction_tags);
        need_send_update = true;
      }
      if (tags_.hash_ != tags_.calc_hash()) {
        LOG(ERROR) << "Receive unexpected Saved Messages tag hash";
      }
      tags_.is_inited_ = true;
      break;
    }
    default:
      UNREACHABLE();
  }

  if (need_send_update) {
    send_update_saved_messages_tags();
  }
  for (auto &promise : promises) {
    promise.set_value(tags_.get_saved_messages_tags_object());
  }
}

td_api::object_ptr<td_api::updateSavedMessagesTags> ReactionManager::get_update_saved_messages_tags_object() const {
  return td_api::make_object<td_api::updateSavedMessagesTags>(tags_.get_saved_messages_tags_object());
}

void ReactionManager::send_update_saved_messages_tags() {
  send_closure(G()->td(), &Td::send_update, get_update_saved_messages_tags_object());
}

void ReactionManager::on_update_saved_reaction_tags(Promise<Unit> &&promise) {
  reget_saved_messages_tags(PromiseCreator::lambda(
      [promise = std::move(promise)](Result<td_api::object_ptr<td_api::savedMessagesTags>> result) mutable {
        promise.set_value(Unit());
      }));
}

void ReactionManager::update_saved_messages_tags(const vector<ReactionType> &old_tags,
                                                 const vector<ReactionType> &new_tags) {
  if (old_tags == new_tags) {
    return;
  }
  bool is_changed = false;
  bool need_reload_title = false;
  tags_.update_saved_messages_tags(old_tags, new_tags, is_changed, need_reload_title);
  if (is_changed) {
    send_update_saved_messages_tags();
  }
  if (need_reload_title && td_->option_manager_->get_option_boolean("is_premium")) {
    on_update_saved_reaction_tags(Auto());
  }
}

void ReactionManager::set_saved_messages_tag_title(ReactionType reaction_type, string title, Promise<Unit> &&promise) {
  if (reaction_type.is_empty()) {
    return promise.set_error(Status::Error(400, "Reaction type must be non-empty"));
  }
  title = clean_name(title, MAX_TAG_TITLE_LENGTH);

  if (tags_.set_tag_title(reaction_type, title)) {
    send_update_saved_messages_tags();
  }

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](Result<Unit> result) mutable {
        if (result.is_ok()) {
          send_closure(actor_id, &ReactionManager::on_update_saved_reaction_tags, std::move(promise));
        } else {
          promise.set_error(result.move_as_error());
        }
      });
  td_->create_handler<UpdateSavedReactionTagQuery>(std::move(query_promise))->send(reaction_type, title);
}

void ReactionManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (!active_reaction_types_.empty()) {
    updates.push_back(get_update_active_emoji_reactions_object());
  }
  if (tags_.is_inited_) {
    updates.push_back(get_update_saved_messages_tags_object());
  }
}

}  // namespace td
