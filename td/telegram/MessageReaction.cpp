//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageReaction.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageSender.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

#include <algorithm>
#include <utility>

namespace td {

class GetMessagesReactionsQuery final : public Td::ResultHandler {
  DialogId dialog_id_;
  vector<MessageId> message_ids_;

 public:
  void send(DialogId dialog_id, vector<MessageId> &&message_ids) {
    dialog_id_ = dialog_id;
    message_ids_ = std::move(message_ids);

    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    CHECK(input_peer != nullptr);

    send_query(G()->net_query_creator().create(telegram_api::messages_getMessagesReactions(
        std::move(input_peer), MessagesManager::get_server_message_ids(message_ids_))));
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

  void send(FullMessageId full_message_id, string reaction, bool is_big) {
    dialog_id_ = full_message_id.get_dialog_id();

    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    int32 flags = 0;
    if (!reaction.empty()) {
      flags |= telegram_api::messages_sendReaction::REACTION_MASK;

      if (is_big) {
        flags |= telegram_api::messages_sendReaction::BIG_MASK;
      }
    }

    send_query(G()->net_query_creator().create(
        telegram_api::messages_sendReaction(flags, false /*ignored*/, std::move(input_peer),
                                            full_message_id.get_message_id().get_server_message_id().get(), reaction),
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
        telegram_api::messages_getMessageReactionsList(
            flags, std::move(input_peer), message_id_.get_server_message_id().get(), reaction_, offset_, limit),
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
      if (!dialog_id.is_valid() ||
          (reaction_.empty() ? reaction->reaction_.empty() : reaction_ != reaction->reaction_)) {
        LOG(ERROR) << "Receive unexpected " << to_string(reaction);
        continue;
      }

      if (offset_.empty()) {
        recent_reactions[reaction->reaction_].push_back(dialog_id);
      }

      auto message_sender = get_min_message_sender_object(td_, dialog_id, "GetMessageReactionsListQuery");
      if (message_sender != nullptr) {
        reactions.push_back(td_api::make_object<td_api::addedReaction>(reaction->reaction_, std::move(message_sender)));
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

void MessageReaction::set_is_chosen(bool is_chosen, DialogId chooser_dialog_id, bool can_get_added_reactions) {
  if (is_chosen_ == is_chosen) {
    return;
  }

  is_chosen_ = is_chosen;

  if (chooser_dialog_id.is_valid()) {
    choose_count_ += is_chosen_ ? 1 : -1;
    if (can_get_added_reactions) {
      td::remove(recent_chooser_dialog_ids_, chooser_dialog_id);
      if (is_chosen_) {
        recent_chooser_dialog_ids_.insert(recent_chooser_dialog_ids_.begin(), chooser_dialog_id);
        if (recent_chooser_dialog_ids_.size() > MAX_RECENT_CHOOSERS) {
          recent_chooser_dialog_ids_.resize(MAX_RECENT_CHOOSERS);
        }
      }
    }
  }
}

td_api::object_ptr<td_api::messageReaction> MessageReaction::get_message_reaction_object(Td *td) const {
  CHECK(!is_empty());

  vector<td_api::object_ptr<td_api::MessageSender>> recent_choosers;
  for (auto dialog_id : recent_chooser_dialog_ids_) {
    auto recent_chooser = get_min_message_sender_object(td, dialog_id, "get_message_reaction_object");
    if (recent_chooser != nullptr) {
      recent_choosers.push_back(std::move(recent_chooser));
    }
  }
  return td_api::make_object<td_api::messageReaction>(reaction_, choose_count_, is_chosen_, std::move(recent_choosers));
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
  return td_api::make_object<td_api::unreadReaction>(reaction_, std::move(sender_id), is_big_);
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
  FlatHashSet<DialogId, DialogIdHash> recent_choosers;
  for (auto &reaction_count : reactions->results_) {
    if (reaction_count->count_ <= 0 || reaction_count->count_ >= MessageReaction::MAX_CHOOSE_COUNT ||
        reaction_count->reaction_.empty()) {
      LOG(ERROR) << "Receive reaction " << reaction_count->reaction_ << " with invalid count "
                 << reaction_count->count_;
      continue;
    }

    if (!reaction_strings.insert(reaction_count->reaction_).second) {
      LOG(ERROR) << "Receive duplicate reaction " << reaction_count->reaction_;
      continue;
    }

    vector<DialogId> recent_chooser_dialog_ids;
    vector<std::pair<ChannelId, MinChannel>> recent_chooser_min_channels;
    for (auto &peer_reaction : reactions->recent_reactions_) {
      if (peer_reaction->reaction_ == reaction_count->reaction_) {
        DialogId dialog_id(peer_reaction->peer_id_);
        if (!dialog_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << dialog_id << " as a recent chooser";
          continue;
        }
        if (!recent_choosers.insert(dialog_id).second) {
          LOG(ERROR) << "Receive duplicate " << dialog_id << " as a recent chooser";
          continue;
        }
        if (!td->messages_manager_->have_dialog_info(dialog_id)) {
          auto dialog_type = dialog_id.get_type();
          if (dialog_type == DialogType::User) {
            auto user_id = dialog_id.get_user_id();
            if (!td->contacts_manager_->have_min_user(user_id)) {
              LOG(ERROR) << "Have no info about " << user_id;
              continue;
            }
          } else if (dialog_type == DialogType::Channel) {
            auto channel_id = dialog_id.get_channel_id();
            auto min_channel = td->contacts_manager_->get_min_channel(channel_id);
            if (min_channel == nullptr) {
              LOG(ERROR) << "Have no info about reacted " << channel_id;
              continue;
            }
            recent_chooser_min_channels.emplace_back(channel_id, *min_channel);
          } else {
            LOG(ERROR) << "Have no info about reacted " << dialog_id;
            continue;
          }
        }

        recent_chooser_dialog_ids.push_back(dialog_id);
        if (peer_reaction->unread_) {
          result->unread_reactions_.emplace_back(std::move(peer_reaction->reaction_), dialog_id, peer_reaction->big_);
        }
        if (recent_chooser_dialog_ids.size() == MessageReaction::MAX_RECENT_CHOOSERS) {
          break;
        }
      }
    }

    result->reactions_.emplace_back(std::move(reaction_count->reaction_), reaction_count->count_,
                                    reaction_count->chosen_, std::move(recent_chooser_dialog_ids),
                                    std::move(recent_chooser_min_channels));
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
  }
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

  // unread_reactions_ are updated independently; compare all other fields
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
                        << " with unread " << reactions.unread_reactions_
                        << " and can_get_added_reactions = " << reactions.can_get_added_reactions_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const unique_ptr<MessageReactions> &reactions) {
  if (reactions == nullptr) {
    return string_builder << "null";
  }
  return string_builder << *reactions;
}

void reload_message_reactions(Td *td, DialogId dialog_id, vector<MessageId> &&message_ids) {
  if (!td->messages_manager_->have_input_peer(dialog_id, AccessRights::Read) || message_ids.empty()) {
    return;
  }

  for (const auto &message_id : message_ids) {
    CHECK(message_id.is_valid());
    CHECK(message_id.is_server());
  }

  td->create_handler<GetMessagesReactionsQuery>()->send(dialog_id, std::move(message_ids));
}

void set_message_reaction(Td *td, FullMessageId full_message_id, string reaction, bool is_big,
                          Promise<Unit> &&promise) {
  td->create_handler<SendReactionQuery>(std::move(promise))->send(full_message_id, std::move(reaction), is_big);
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

}  // namespace td
