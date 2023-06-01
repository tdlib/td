//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageReaction.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageSender.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "td/actor/actor.h"

#include "td/utils/algorithm.h"
#include "td/utils/as.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/crypto.h"
#include "td/utils/emoji.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"
#include "td/utils/utf8.h"

#include <algorithm>
#include <utility>

namespace td {

static size_t get_max_reaction_count() {
  bool is_premium = G()->get_option_boolean("is_premium");
  auto option_key = is_premium ? Slice("reactions_user_max_premium") : Slice("reactions_user_max_default");
  return static_cast<size_t>(
      max(static_cast<int32>(1), static_cast<int32>(G()->get_option_integer(option_key, is_premium ? 3 : 1))));
}

static int64 get_custom_emoji_id(const string &reaction) {
  auto r_decoded = base64_decode(Slice(&reaction[1], reaction.size() - 1));
  CHECK(r_decoded.is_ok());
  CHECK(r_decoded.ok().size() == 8);
  return as<int64>(r_decoded.ok().c_str());
}

static string get_custom_emoji_string(int64 custom_emoji_id) {
  char s[8];
  as<int64>(&s) = custom_emoji_id;
  return PSTRING() << '#' << base64_encode(Slice(s, 8));
}

telegram_api::object_ptr<telegram_api::Reaction> get_input_reaction(const string &reaction) {
  if (reaction.empty()) {
    return telegram_api::make_object<telegram_api::reactionEmpty>();
  }
  if (is_custom_reaction(reaction)) {
    return telegram_api::make_object<telegram_api::reactionCustomEmoji>(get_custom_emoji_id(reaction));
  }
  return telegram_api::make_object<telegram_api::reactionEmoji>(reaction);
}

string get_message_reaction_string(const telegram_api::object_ptr<telegram_api::Reaction> &reaction) {
  if (reaction == nullptr) {
    return string();
  }
  switch (reaction->get_id()) {
    case telegram_api::reactionEmpty::ID:
      return string();
    case telegram_api::reactionEmoji::ID: {
      const string &emoji = static_cast<const telegram_api::reactionEmoji *>(reaction.get())->emoticon_;
      if (is_custom_reaction(emoji)) {
        return string();
      }
      return emoji;
    }
    case telegram_api::reactionCustomEmoji::ID:
      return get_custom_emoji_string(
          static_cast<const telegram_api::reactionCustomEmoji *>(reaction.get())->document_id_);
    default:
      UNREACHABLE();
      return string();
  }
}

td_api::object_ptr<td_api::ReactionType> get_reaction_type_object(const string &reaction) {
  CHECK(!reaction.empty());
  if (is_custom_reaction(reaction)) {
    return td_api::make_object<td_api::reactionTypeCustomEmoji>(get_custom_emoji_id(reaction));
  }
  return td_api::make_object<td_api::reactionTypeEmoji>(reaction);
}

string get_message_reaction_string(const td_api::object_ptr<td_api::ReactionType> &type) {
  if (type == nullptr) {
    return string();
  }
  switch (type->get_id()) {
    case td_api::reactionTypeEmoji::ID: {
      const string &emoji = static_cast<const td_api::reactionTypeEmoji *>(type.get())->emoji_;
      if (!check_utf8(emoji) || is_custom_reaction(emoji)) {
        return string();
      }
      return emoji;
    }
    case td_api::reactionTypeCustomEmoji::ID:
      return get_custom_emoji_string(
          static_cast<const td_api::reactionTypeCustomEmoji *>(type.get())->custom_emoji_id_);
    default:
      UNREACHABLE();
      return string();
  }
}

class GetMessagesReactionsQuery final : public Td::ResultHandler {
  DialogId dialog_id_;
  vector<MessageId> message_ids_;

 public:
  void send(DialogId dialog_id, vector<MessageId> &&message_ids) {
    dialog_id_ = dialog_id;
    message_ids_ = std::move(message_ids);

    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    CHECK(input_peer != nullptr);

    send_query(
        G()->net_query_creator().create(telegram_api::messages_getMessagesReactions(
                                            std::move(input_peer), MessageId::get_server_message_ids(message_ids_)),
                                        {{dialog_id_}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getMessagesReactions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetMessagesReactionsQuery: " << to_string(ptr);
    if (ptr->get_id() == telegram_api::updates::ID) {
      auto &updates = static_cast<telegram_api::updates *>(ptr.get())->updates_;
      FlatHashSet<MessageId, MessageIdHash> skipped_message_ids;
      for (auto message_id : message_ids_) {
        skipped_message_ids.insert(message_id);
      }
      for (const auto &update : updates) {
        if (update->get_id() == telegram_api::updateMessageReactions::ID) {
          auto update_message_reactions = static_cast<const telegram_api::updateMessageReactions *>(update.get());
          if (DialogId(update_message_reactions->peer_) == dialog_id_) {
            skipped_message_ids.erase(MessageId(ServerMessageId(update_message_reactions->msg_id_)));
          }
        }
      }
      for (auto message_id : skipped_message_ids) {
        td_->messages_manager_->update_message_reactions({dialog_id_, message_id}, nullptr);
      }
    }
    td_->updates_manager_->on_get_updates(std::move(ptr), Promise<Unit>());
    td_->messages_manager_->try_reload_message_reactions(dialog_id_, true);
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "GetMessagesReactionsQuery");
    td_->messages_manager_->try_reload_message_reactions(dialog_id_, true);
  }
};

class SendReactionQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit SendReactionQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(FullMessageId full_message_id, vector<string> reactions, bool is_big, bool add_to_recent) {
    dialog_id_ = full_message_id.get_dialog_id();

    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    int32 flags = 0;
    if (!reactions.empty()) {
      flags |= telegram_api::messages_sendReaction::REACTION_MASK;

      if (is_big) {
        flags |= telegram_api::messages_sendReaction::BIG_MASK;
      }

      if (add_to_recent) {
        flags |= telegram_api::messages_sendReaction::ADD_TO_RECENT_MASK;
      }
    }

    send_query(G()->net_query_creator().create(
        telegram_api::messages_sendReaction(flags, false /*ignored*/, false /*ignored*/, std::move(input_peer),
                                            full_message_id.get_message_id().get_server_message_id().get(),
                                            transform(reactions, get_input_reaction)),
        {{dialog_id_}, {full_message_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_sendReaction>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SendReactionQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (status.message() == "MESSAGE_NOT_MODIFIED") {
      return promise_.set_value(Unit());
    }
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "SendReactionQuery");
    promise_.set_error(std::move(status));
  }
};

class GetMessageReactionsListQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::addedReactions>> promise_;
  DialogId dialog_id_;
  MessageId message_id_;
  string reaction_;
  string offset_;

 public:
  explicit GetMessageReactionsListQuery(Promise<td_api::object_ptr<td_api::addedReactions>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(FullMessageId full_message_id, string reaction, string offset, int32 limit) {
    dialog_id_ = full_message_id.get_dialog_id();
    message_id_ = full_message_id.get_message_id();
    reaction_ = std::move(reaction);
    offset_ = std::move(offset);

    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    int32 flags = 0;
    if (!reaction_.empty()) {
      flags |= telegram_api::messages_getMessageReactionsList::REACTION_MASK;
    }
    if (!offset_.empty()) {
      flags |= telegram_api::messages_getMessageReactionsList::OFFSET_MASK;
    }

    send_query(G()->net_query_creator().create(
        telegram_api::messages_getMessageReactionsList(flags, std::move(input_peer),
                                                       message_id_.get_server_message_id().get(),
                                                       get_input_reaction(reaction_), offset_, limit),
        {{full_message_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getMessageReactionsList>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetMessageReactionsListQuery: " << to_string(ptr);

    td_->contacts_manager_->on_get_users(std::move(ptr->users_), "GetMessageReactionsListQuery");
    td_->contacts_manager_->on_get_chats(std::move(ptr->chats_), "GetMessageReactionsListQuery");

    int32 total_count = ptr->count_;
    auto received_reaction_count = static_cast<int32>(ptr->reactions_.size());
    if (total_count < received_reaction_count) {
      LOG(ERROR) << "Receive invalid total_count in " << to_string(ptr);
      total_count = received_reaction_count;
    }

    vector<td_api::object_ptr<td_api::addedReaction>> reactions;
    FlatHashMap<string, vector<DialogId>> recent_reactions;
    for (const auto &reaction : ptr->reactions_) {
      DialogId dialog_id(reaction->peer_id_);
      auto reaction_str = get_message_reaction_string(reaction->reaction_);
      if (!dialog_id.is_valid() || (reaction_.empty() ? reaction_str.empty() : reaction_ != reaction_str)) {
        LOG(ERROR) << "Receive unexpected " << to_string(reaction);
        continue;
      }

      if (offset_.empty()) {
        recent_reactions[reaction_str].push_back(dialog_id);
      }

      auto message_sender = get_min_message_sender_object(td_, dialog_id, "GetMessageReactionsListQuery");
      if (message_sender != nullptr) {
        reactions.push_back(td_api::make_object<td_api::addedReaction>(get_reaction_type_object(reaction_str),
                                                                       std::move(message_sender), reaction->date_));
      }
    }

    if (offset_.empty()) {
      td_->messages_manager_->on_get_message_reaction_list({dialog_id_, message_id_}, reaction_,
                                                           std::move(recent_reactions), total_count);
    }

    promise_.set_value(
        td_api::make_object<td_api::addedReactions>(total_count, std::move(reactions), ptr->next_offset_));
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "GetMessageReactionsListQuery");
    promise_.set_error(std::move(status));
  }
};

class SetDefaultReactionQuery final : public Td::ResultHandler {
  string reaction_;

 public:
  void send(const string &reaction) {
    reaction_ = reaction;
    send_query(
        G()->net_query_creator().create(telegram_api::messages_setDefaultReaction(get_input_reaction(reaction))));
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
    if (default_reaction != reaction_) {
      send_set_default_reaction_query(td_);
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

class ReportReactionQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit ReportReactionQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, MessageId message_id, DialogId chooser_dialog_id) {
    dialog_id_ = dialog_id;

    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    CHECK(input_peer != nullptr);

    auto chooser_input_peer = td_->messages_manager_->get_input_peer(chooser_dialog_id, AccessRights::Know);
    if (chooser_input_peer == nullptr) {
      return promise_.set_error(Status::Error(400, "Reaction sender is not accessible"));
    }

    send_query(G()->net_query_creator().create(telegram_api::messages_reportReaction(
        std::move(input_peer), message_id.get_server_message_id().get(), std::move(chooser_input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_reportReaction>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "ReportReactionQuery");
    promise_.set_error(std::move(status));
  }
};

void MessageReaction::add_recent_chooser_dialog_id(DialogId dialog_id) {
  recent_chooser_dialog_ids_.insert(recent_chooser_dialog_ids_.begin(), dialog_id);
  if (recent_chooser_dialog_ids_.size() > MAX_RECENT_CHOOSERS + 1) {
    LOG(ERROR) << "Have " << recent_chooser_dialog_ids_.size() << " recent reaction choosers";
    recent_chooser_dialog_ids_.resize(MAX_RECENT_CHOOSERS + 1);
  }
}

bool MessageReaction::remove_recent_chooser_dialog_id(DialogId dialog_id) {
  return td::remove(recent_chooser_dialog_ids_, dialog_id);
}

void MessageReaction::update_recent_chooser_dialog_ids(const MessageReaction &old_reaction) {
  if (recent_chooser_dialog_ids_.size() != MAX_RECENT_CHOOSERS) {
    return;
  }
  CHECK(is_chosen_ && old_reaction.is_chosen_);
  CHECK(reaction_ == old_reaction.reaction_);
  CHECK(old_reaction.recent_chooser_dialog_ids_.size() == MAX_RECENT_CHOOSERS + 1);
  for (size_t i = 0; i < MAX_RECENT_CHOOSERS; i++) {
    if (recent_chooser_dialog_ids_[i] != old_reaction.recent_chooser_dialog_ids_[i]) {
      return;
    }
  }
  recent_chooser_dialog_ids_ = old_reaction.recent_chooser_dialog_ids_;
  recent_chooser_min_channels_ = old_reaction.recent_chooser_min_channels_;
}

void MessageReaction::set_is_chosen(bool is_chosen, DialogId chooser_dialog_id, bool have_recent_choosers) {
  if (is_chosen_ == is_chosen) {
    return;
  }

  is_chosen_ = is_chosen;

  if (chooser_dialog_id.is_valid()) {
    choose_count_ += is_chosen_ ? 1 : -1;
    if (have_recent_choosers) {
      remove_recent_chooser_dialog_id(chooser_dialog_id);
      if (is_chosen_) {
        add_recent_chooser_dialog_id(chooser_dialog_id);
      }
    }
  }
}

td_api::object_ptr<td_api::messageReaction> MessageReaction::get_message_reaction_object(Td *td, UserId my_user_id,
                                                                                         UserId peer_user_id) const {
  CHECK(!is_empty());

  vector<td_api::object_ptr<td_api::MessageSender>> recent_choosers;
  if (my_user_id.is_valid()) {
    CHECK(peer_user_id.is_valid());
    if (is_chosen()) {
      auto recent_chooser = get_min_message_sender_object(td, DialogId(my_user_id), "get_message_reaction_object");
      if (recent_chooser != nullptr) {
        recent_choosers.push_back(std::move(recent_chooser));
      }
    }
    if (choose_count_ >= (is_chosen() ? 2 : 1)) {
      auto recent_chooser = get_min_message_sender_object(td, DialogId(peer_user_id), "get_message_reaction_object");
      if (recent_chooser != nullptr) {
        recent_choosers.push_back(std::move(recent_chooser));
      }
    }
  } else {
    for (auto dialog_id : recent_chooser_dialog_ids_) {
      auto recent_chooser = get_min_message_sender_object(td, dialog_id, "get_message_reaction_object");
      if (recent_chooser != nullptr) {
        recent_choosers.push_back(std::move(recent_chooser));
        if (recent_choosers.size() == MAX_RECENT_CHOOSERS) {
          break;
        }
      }
    }
  }
  return td_api::make_object<td_api::messageReaction>(get_reaction_type_object(reaction_), choose_count_, is_chosen_,
                                                      std::move(recent_choosers));
}

bool operator==(const MessageReaction &lhs, const MessageReaction &rhs) {
  return lhs.reaction_ == rhs.reaction_ && lhs.choose_count_ == rhs.choose_count_ && lhs.is_chosen_ == rhs.is_chosen_ &&
         lhs.recent_chooser_dialog_ids_ == rhs.recent_chooser_dialog_ids_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageReaction &reaction) {
  string_builder << '[' << reaction.reaction_ << (reaction.is_chosen_ ? " X " : " x ") << reaction.choose_count_;
  if (!reaction.recent_chooser_dialog_ids_.empty()) {
    string_builder << " by " << reaction.recent_chooser_dialog_ids_;
  }
  return string_builder << ']';
}

td_api::object_ptr<td_api::unreadReaction> UnreadMessageReaction::get_unread_reaction_object(Td *td) const {
  auto sender_id = get_min_message_sender_object(td, sender_dialog_id_, "get_unread_reaction_object");
  if (sender_id == nullptr) {
    return nullptr;
  }
  return td_api::make_object<td_api::unreadReaction>(get_reaction_type_object(reaction_), std::move(sender_id),
                                                     is_big_);
}

bool operator==(const UnreadMessageReaction &lhs, const UnreadMessageReaction &rhs) {
  return lhs.reaction_ == rhs.reaction_ && lhs.sender_dialog_id_ == rhs.sender_dialog_id_ && lhs.is_big_ == rhs.is_big_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const UnreadMessageReaction &unread_reaction) {
  return string_builder << '[' << unread_reaction.reaction_ << (unread_reaction.is_big_ ? " BY " : " by ")
                        << unread_reaction.sender_dialog_id_ << ']';
}

unique_ptr<MessageReactions> MessageReactions::get_message_reactions(
    Td *td, tl_object_ptr<telegram_api::messageReactions> &&reactions, bool is_bot) {
  if (reactions == nullptr || is_bot) {
    return nullptr;
  }

  auto result = make_unique<MessageReactions>();
  result->can_get_added_reactions_ = reactions->can_see_list_;
  result->is_min_ = reactions->min_;

  FlatHashSet<string> reaction_strings;
  vector<std::pair<int32, string>> chosen_reaction_order;
  for (auto &reaction_count : reactions->results_) {
    auto reaction_str = get_message_reaction_string(reaction_count->reaction_);
    if (reaction_count->count_ <= 0 || reaction_count->count_ >= MessageReaction::MAX_CHOOSE_COUNT ||
        reaction_str.empty()) {
      LOG(ERROR) << "Receive reaction " << reaction_str << " with invalid count " << reaction_count->count_;
      continue;
    }

    if (!reaction_strings.insert(reaction_str).second) {
      LOG(ERROR) << "Receive duplicate reaction " << reaction_str;
      continue;
    }

    FlatHashSet<DialogId, DialogIdHash> recent_choosers;
    vector<DialogId> recent_chooser_dialog_ids;
    vector<std::pair<ChannelId, MinChannel>> recent_chooser_min_channels;
    for (auto &peer_reaction : reactions->recent_reactions_) {
      auto peer_reaction_str = get_message_reaction_string(peer_reaction->reaction_);
      if (peer_reaction_str == reaction_str) {
        DialogId dialog_id(peer_reaction->peer_id_);
        if (!dialog_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << dialog_id << " as a recent chooser for reaction " << reaction_str;
          continue;
        }
        if (!recent_choosers.insert(dialog_id).second) {
          LOG(ERROR) << "Receive duplicate " << dialog_id << " as a recent chooser for reaction " << reaction_str;
          continue;
        }
        if (!td->messages_manager_->have_dialog_info(dialog_id)) {
          auto dialog_type = dialog_id.get_type();
          if (dialog_type == DialogType::User) {
            auto user_id = dialog_id.get_user_id();
            if (!td->contacts_manager_->have_min_user(user_id)) {
              LOG(ERROR) << "Receive unknown " << user_id;
              continue;
            }
          } else if (dialog_type == DialogType::Channel) {
            auto channel_id = dialog_id.get_channel_id();
            auto min_channel = td->contacts_manager_->get_min_channel(channel_id);
            if (min_channel == nullptr) {
              LOG(ERROR) << "Receive unknown reacted " << channel_id;
              continue;
            }
            recent_chooser_min_channels.emplace_back(channel_id, *min_channel);
          } else {
            LOG(ERROR) << "Receive unknown reacted " << dialog_id;
            continue;
          }
        }

        recent_chooser_dialog_ids.push_back(dialog_id);
        if (peer_reaction->unread_) {
          result->unread_reactions_.emplace_back(std::move(peer_reaction_str), dialog_id, peer_reaction->big_);
        }
        if (recent_chooser_dialog_ids.size() == MessageReaction::MAX_RECENT_CHOOSERS) {
          break;
        }
      }
    }

    bool is_chosen = (reaction_count->flags_ & telegram_api::reactionCount::CHOSEN_ORDER_MASK) != 0;
    if (is_chosen) {
      chosen_reaction_order.emplace_back(reaction_count->chosen_order_, reaction_str);
    }
    result->reactions_.push_back({std::move(reaction_str), reaction_count->count_, is_chosen,
                                  std::move(recent_chooser_dialog_ids), std::move(recent_chooser_min_channels)});
  }
  if (chosen_reaction_order.size() > 1) {
    std::sort(chosen_reaction_order.begin(), chosen_reaction_order.end());
    result->chosen_reaction_order_ =
        transform(chosen_reaction_order, [](const std::pair<int32, string> &order) { return order.second; });
  }
  return result;
}

MessageReaction *MessageReactions::get_reaction(const string &reaction) {
  for (auto &added_reaction : reactions_) {
    if (added_reaction.get_reaction() == reaction) {
      return &added_reaction;
    }
  }
  return nullptr;
}

const MessageReaction *MessageReactions::get_reaction(const string &reaction) const {
  for (auto &added_reaction : reactions_) {
    if (added_reaction.get_reaction() == reaction) {
      return &added_reaction;
    }
  }
  return nullptr;
}

void MessageReactions::update_from(const MessageReactions &old_reactions) {
  if (is_min_ && !old_reactions.is_min_) {
    // chosen reaction was known, keep it
    is_min_ = false;
    for (const auto &old_reaction : old_reactions.reactions_) {
      if (old_reaction.is_chosen()) {
        auto *reaction = get_reaction(old_reaction.get_reaction());
        if (reaction != nullptr) {
          reaction->set_is_chosen(true, DialogId(), false);
        }
      }
    }
    unread_reactions_ = old_reactions.unread_reactions_;
    chosen_reaction_order_ = old_reactions.chosen_reaction_order_;
  }
  for (const auto &old_reaction : old_reactions.reactions_) {
    if (old_reaction.is_chosen() &&
        old_reaction.get_recent_chooser_dialog_ids().size() == MessageReaction::MAX_RECENT_CHOOSERS + 1) {
      auto *reaction = get_reaction(old_reaction.get_reaction());
      if (reaction != nullptr && reaction->is_chosen()) {
        reaction->update_recent_chooser_dialog_ids(old_reaction);
      }
    }
  }
}

bool MessageReactions::add_reaction(const string &reaction, bool is_big, DialogId chooser_dialog_id,
                                    bool have_recent_choosers) {
  vector<string> new_chosen_reaction_order = get_chosen_reactions();

  auto added_reaction = get_reaction(reaction);
  if (added_reaction == nullptr) {
    vector<DialogId> recent_chooser_dialog_ids;
    if (have_recent_choosers) {
      recent_chooser_dialog_ids.push_back(chooser_dialog_id);
    }
    reactions_.push_back({reaction, 1, true, std::move(recent_chooser_dialog_ids), Auto()});
    new_chosen_reaction_order.emplace_back(reaction);
  } else if (!added_reaction->is_chosen()) {
    added_reaction->set_is_chosen(true, chooser_dialog_id, have_recent_choosers);
    new_chosen_reaction_order.emplace_back(reaction);
  } else if (!is_big) {
    return false;
  }

  auto max_reaction_count = get_max_reaction_count();
  while (new_chosen_reaction_order.size() > max_reaction_count) {
    auto index = new_chosen_reaction_order[0] == reaction ? 1 : 0;
    CHECK(static_cast<size_t>(index) < new_chosen_reaction_order.size());
    bool is_removed = do_remove_reaction(new_chosen_reaction_order[index], chooser_dialog_id, have_recent_choosers);
    CHECK(is_removed);
    new_chosen_reaction_order.erase(new_chosen_reaction_order.begin() + index);
  }

  if (new_chosen_reaction_order.size() == 1) {
    new_chosen_reaction_order.clear();
  }
  chosen_reaction_order_ = std::move(new_chosen_reaction_order);
  return true;
}

bool MessageReactions::remove_reaction(const string &reaction, DialogId chooser_dialog_id, bool have_recent_choosers) {
  if (do_remove_reaction(reaction, chooser_dialog_id, have_recent_choosers)) {
    if (!chosen_reaction_order_.empty()) {
      bool is_removed = td::remove(chosen_reaction_order_, reaction);
      CHECK(is_removed);

      auto max_reaction_count = get_max_reaction_count();
      while (chosen_reaction_order_.size() > max_reaction_count) {
        is_removed = do_remove_reaction(chosen_reaction_order_[0], chooser_dialog_id, have_recent_choosers);
        CHECK(is_removed);
        chosen_reaction_order_.erase(chosen_reaction_order_.begin());
      }

      if (chosen_reaction_order_.size() <= 1) {
        reset_to_empty(chosen_reaction_order_);
      }
    }

    return true;
  }
  return false;
}

bool MessageReactions::do_remove_reaction(const string &reaction, DialogId chooser_dialog_id,
                                          bool have_recent_choosers) {
  for (auto it = reactions_.begin(); it != reactions_.end(); ++it) {
    auto &message_reaction = *it;
    if (message_reaction.get_reaction() == reaction) {
      if (message_reaction.is_chosen()) {
        message_reaction.set_is_chosen(false, chooser_dialog_id, have_recent_choosers);
        if (message_reaction.is_empty()) {
          it = reactions_.erase(it);
        }
        return true;
      }
      break;
    }
  }
  return false;
}

void MessageReactions::sort_reactions(const FlatHashMap<string, size_t> &active_reaction_pos) {
  std::sort(reactions_.begin(), reactions_.end(),
            [&active_reaction_pos](const MessageReaction &lhs, const MessageReaction &rhs) {
              if (lhs.get_choose_count() != rhs.get_choose_count()) {
                return lhs.get_choose_count() > rhs.get_choose_count();
              }
              auto lhs_it = active_reaction_pos.find(lhs.get_reaction());
              auto lhs_pos = lhs_it != active_reaction_pos.end() ? lhs_it->second : active_reaction_pos.size();
              auto rhs_it = active_reaction_pos.find(rhs.get_reaction());
              auto rhs_pos = rhs_it != active_reaction_pos.end() ? rhs_it->second : active_reaction_pos.size();
              if (lhs_pos != rhs_pos) {
                return lhs_pos < rhs_pos;
              }

              return lhs.get_reaction() < rhs.get_reaction();
            });
}

void MessageReactions::fix_chosen_reaction(DialogId my_dialog_id) {
  bool need_fix = false;
  for (auto &reaction : reactions_) {
    if (!reaction.is_chosen() && reaction.remove_recent_chooser_dialog_id(my_dialog_id)) {
      LOG(WARNING) << "Fix recent chosen reaction in " << *this;
      need_fix = true;
    }
  }
  if (!need_fix) {
    return;
  }
  for (auto &reaction : reactions_) {
    if (reaction.is_chosen() && !td::contains(reaction.get_recent_chooser_dialog_ids(), my_dialog_id)) {
      reaction.add_recent_chooser_dialog_id(my_dialog_id);
    }
  }
}

vector<string> MessageReactions::get_chosen_reactions() const {
  if (!chosen_reaction_order_.empty()) {
    return chosen_reaction_order_;
  }

  vector<string> reaction_order;
  for (auto &reaction : reactions_) {
    if (reaction.is_chosen()) {
      reaction_order.push_back(reaction.get_reaction());
    }
  }
  return reaction_order;
}

bool MessageReactions::are_consistent_with_list(const string &reaction, FlatHashMap<string, vector<DialogId>> reactions,
                                                int32 total_count) const {
  auto are_consistent = [](const vector<DialogId> &lhs, const vector<DialogId> &rhs) {
    size_t i = 0;
    size_t max_i = td::min(lhs.size(), rhs.size());
    while (i < max_i && lhs[i] == rhs[i]) {
      i++;
    }
    return i == max_i;
  };

  if (reaction.empty()) {
    // received list and total_count for all reactions
    int32 old_total_count = 0;
    for (const auto &message_reaction : reactions_) {
      CHECK(!message_reaction.get_reaction().empty());
      if (!are_consistent(reactions[message_reaction.get_reaction()],
                          message_reaction.get_recent_chooser_dialog_ids())) {
        return false;
      }
      old_total_count += message_reaction.get_choose_count();
      reactions.erase(message_reaction.get_reaction());
    }
    return old_total_count == total_count && reactions.empty();
  }

  // received list and total_count for a single reaction
  const auto *message_reaction = get_reaction(reaction);
  if (message_reaction == nullptr) {
    return reactions.count(reaction) == 0 && total_count == 0;
  } else {
    return are_consistent(reactions[reaction], message_reaction->get_recent_chooser_dialog_ids()) &&
           message_reaction->get_choose_count() == total_count;
  }
}

vector<td_api::object_ptr<td_api::messageReaction>> MessageReactions::get_message_reactions_object(
    Td *td, UserId my_user_id, UserId peer_user_id) const {
  return transform(reactions_, [td, my_user_id, peer_user_id](const MessageReaction &reaction) {
    return reaction.get_message_reaction_object(td, my_user_id, peer_user_id);
  });
}

void MessageReactions::add_min_channels(Td *td) const {
  for (const auto &reaction : reactions_) {
    for (const auto &recent_chooser_min_channel : reaction.get_recent_chooser_min_channels()) {
      LOG(INFO) << "Add min reacted " << recent_chooser_min_channel.first;
      td->contacts_manager_->add_min_channel(recent_chooser_min_channel.first, recent_chooser_min_channel.second);
    }
  }
}

void MessageReactions::add_dependencies(Dependencies &dependencies) const {
  for (const auto &reaction : reactions_) {
    const auto &dialog_ids = reaction.get_recent_chooser_dialog_ids();
    for (auto dialog_id : dialog_ids) {
      dependencies.add_message_sender_dependencies(dialog_id);
    }
  }
}

bool MessageReactions::need_update_message_reactions(const MessageReactions *old_reactions,
                                                     const MessageReactions *new_reactions) {
  if (old_reactions == nullptr) {
    // add reactions
    return new_reactions != nullptr;
  }
  if (new_reactions == nullptr) {
    // remove reactions when they are disabled
    return true;
  }

  // unread_reactions_ and chosen_reaction_order_ are updated independently; compare all other fields
  return old_reactions->reactions_ != new_reactions->reactions_ || old_reactions->is_min_ != new_reactions->is_min_ ||
         old_reactions->can_get_added_reactions_ != new_reactions->can_get_added_reactions_ ||
         old_reactions->need_polling_ != new_reactions->need_polling_;
}

bool MessageReactions::need_update_unread_reactions(const MessageReactions *old_reactions,
                                                    const MessageReactions *new_reactions) {
  if (old_reactions == nullptr || old_reactions->unread_reactions_.empty()) {
    return !(new_reactions == nullptr || new_reactions->unread_reactions_.empty());
  }
  return new_reactions == nullptr || old_reactions->unread_reactions_ != new_reactions->unread_reactions_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageReactions &reactions) {
  return string_builder << (reactions.is_min_ ? "Min" : "") << "MessageReactions{" << reactions.reactions_
                        << " with unread " << reactions.unread_reactions_ << ", reaction order "
                        << reactions.chosen_reaction_order_
                        << " and can_get_added_reactions = " << reactions.can_get_added_reactions_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const unique_ptr<MessageReactions> &reactions) {
  if (reactions == nullptr) {
    return string_builder << "null";
  }
  return string_builder << *reactions;
}

bool is_custom_reaction(const string &reaction) {
  return reaction[0] == '#';
}

bool is_active_reaction(const string &reaction, const FlatHashMap<string, size_t> &active_reaction_pos) {
  return !reaction.empty() && (is_custom_reaction(reaction) || active_reaction_pos.count(reaction) > 0);
}

void reload_message_reactions(Td *td, DialogId dialog_id, vector<MessageId> &&message_ids) {
  if (!td->messages_manager_->have_input_peer(dialog_id, AccessRights::Read) ||
      dialog_id.get_type() == DialogType::SecretChat || message_ids.empty()) {
    return;
  }

  for (const auto &message_id : message_ids) {
    CHECK(message_id.is_valid());
    CHECK(message_id.is_server());
  }

  td->create_handler<GetMessagesReactionsQuery>()->send(dialog_id, std::move(message_ids));
}

void send_message_reaction(Td *td, FullMessageId full_message_id, vector<string> reactions, bool is_big,
                           bool add_to_recent, Promise<Unit> &&promise) {
  td->create_handler<SendReactionQuery>(std::move(promise))
      ->send(full_message_id, std::move(reactions), is_big, add_to_recent);
}

void get_message_added_reactions(Td *td, FullMessageId full_message_id, string reaction, string offset, int32 limit,
                                 Promise<td_api::object_ptr<td_api::addedReactions>> &&promise) {
  if (!td->messages_manager_->have_message_force(full_message_id, "get_message_added_reactions")) {
    return promise.set_error(Status::Error(400, "Message not found"));
  }

  auto message_id = full_message_id.get_message_id();
  if (full_message_id.get_dialog_id().get_type() == DialogType::SecretChat || !message_id.is_valid() ||
      !message_id.is_server()) {
    return promise.set_value(td_api::make_object<td_api::addedReactions>(0, Auto(), string()));
  }

  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }
  static constexpr int32 MAX_GET_ADDED_REACTIONS = 100;  // server side limit
  if (limit > MAX_GET_ADDED_REACTIONS) {
    limit = MAX_GET_ADDED_REACTIONS;
  }

  td->create_handler<GetMessageReactionsListQuery>(std::move(promise))
      ->send(full_message_id, std::move(reaction), std::move(offset), limit);
}

void set_default_reaction(Td *td, string reaction, Promise<Unit> &&promise) {
  if (reaction.empty()) {
    return promise.set_error(Status::Error(400, "Default reaction must be non-empty"));
  }
  if (!is_custom_reaction(reaction) && !td->stickers_manager_->is_active_reaction(reaction)) {
    return promise.set_error(Status::Error(400, "Can't set incative reaction as default"));
  }

  if (td->option_manager_->get_option_string("default_reaction", "-") != reaction) {
    td->option_manager_->set_option_string("default_reaction", reaction);
    if (!td->option_manager_->get_option_boolean("default_reaction_needs_sync")) {
      td->option_manager_->set_option_boolean("default_reaction_needs_sync", true);
      send_set_default_reaction_query(td);
    }
  }
  promise.set_value(Unit());
}

void send_set_default_reaction_query(Td *td) {
  td->create_handler<SetDefaultReactionQuery>()->send(td->option_manager_->get_option_string("default_reaction"));
}

td_api::object_ptr<td_api::updateDefaultReactionType> get_update_default_reaction_type(const string &default_reaction) {
  if (default_reaction.empty()) {
    return nullptr;
  }
  return td_api::make_object<td_api::updateDefaultReactionType>(get_reaction_type_object(default_reaction));
}

void report_message_reactions(Td *td, FullMessageId full_message_id, DialogId chooser_dialog_id,
                              Promise<Unit> &&promise) {
  auto dialog_id = full_message_id.get_dialog_id();
  if (!td->messages_manager_->have_dialog_force(dialog_id, "send_callback_query")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (!td->messages_manager_->have_input_peer(dialog_id, AccessRights::Read)) {
    return promise.set_error(Status::Error(400, "Can't access the chat"));
  }
  if (dialog_id.get_type() == DialogType::SecretChat) {
    return promise.set_error(Status::Error(400, "Reactions can't be reported in the chat"));
  }

  if (!td->messages_manager_->have_message_force(full_message_id, "report_user_reactions")) {
    return promise.set_error(Status::Error(400, "Message not found"));
  }
  auto message_id = full_message_id.get_message_id();
  if (message_id.is_valid_scheduled()) {
    return promise.set_error(Status::Error(400, "Can't report reactions on scheduled messages"));
  }
  if (!message_id.is_server()) {
    return promise.set_error(Status::Error(400, "Message reactions can't be reported"));
  }

  if (!td->messages_manager_->have_input_peer(chooser_dialog_id, AccessRights::Know)) {
    return promise.set_error(Status::Error(400, "Reaction sender not found"));
  }

  td->create_handler<ReportReactionQuery>(std::move(promise))->send(dialog_id, message_id, chooser_dialog_id);
}

vector<string> get_recent_reactions(Td *td) {
  return td->stickers_manager_->get_recent_reactions();
}

vector<string> get_top_reactions(Td *td) {
  return td->stickers_manager_->get_top_reactions();
}

void add_recent_reaction(Td *td, const string &reaction) {
  td->stickers_manager_->add_recent_reaction(reaction);
}

int64 get_reactions_hash(const vector<string> &reactions) {
  vector<uint64> numbers;
  for (auto &reaction : reactions) {
    if (is_custom_reaction(reaction)) {
      auto custom_emoji_id = static_cast<uint64>(get_custom_emoji_id(reaction));
      numbers.push_back(custom_emoji_id >> 32);
      numbers.push_back(custom_emoji_id & 0xFFFFFFFF);
    } else {
      auto emoji = remove_emoji_selectors(reaction);
      unsigned char hash[16];
      md5(emoji, {hash, sizeof(hash)});
      auto get = [hash](int num) {
        return static_cast<uint32>(hash[num]);
      };

      numbers.push_back(0);
      numbers.push_back(static_cast<int32>((get(0) << 24) + (get(1) << 16) + (get(2) << 8) + get(3)));
    }
  }
  return get_vector_hash(numbers);
}

}  // namespace td
