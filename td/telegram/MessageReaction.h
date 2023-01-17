//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MinChannel.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/StringBuilder.h"

#include <utility>

namespace td {

class Dependencies;

class Td;

class MessageReaction {
  static constexpr int32 MAX_CHOOSE_COUNT = 2147483640;

  static constexpr size_t MAX_RECENT_CHOOSERS = 3;

  string reaction_;
  int32 choose_count_ = 0;
  bool is_chosen_ = false;
  vector<DialogId> recent_chooser_dialog_ids_;
  vector<std::pair<ChannelId, MinChannel>> recent_chooser_min_channels_;

  friend bool operator==(const MessageReaction &lhs, const MessageReaction &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const MessageReaction &message_reaction);

  friend struct MessageReactions;

  MessageReaction(string reaction, int32 choose_count, bool is_chosen, vector<DialogId> &&recent_chooser_dialog_ids,
                  vector<std::pair<ChannelId, MinChannel>> &&recent_chooser_min_channels)
      : reaction_(std::move(reaction))
      , choose_count_(choose_count)
      , is_chosen_(is_chosen)
      , recent_chooser_dialog_ids_(std::move(recent_chooser_dialog_ids))
      , recent_chooser_min_channels_(std::move(recent_chooser_min_channels)) {
  }

  bool is_empty() const {
    return choose_count_ <= 0;
  }

  bool is_chosen() const {
    return is_chosen_;
  }

  void set_is_chosen(bool is_chosen, DialogId chooser_dialog_id, bool have_recent_choosers);

  void add_recent_chooser_dialog_id(DialogId dialog_id);

  bool remove_recent_chooser_dialog_id(DialogId dialog_id);

  void update_recent_chooser_dialog_ids(const MessageReaction &old_reaction);

  int32 get_choose_count() const {
    return choose_count_;
  }

  const vector<DialogId> &get_recent_chooser_dialog_ids() const {
    return recent_chooser_dialog_ids_;
  }

  const vector<std::pair<ChannelId, MinChannel>> &get_recent_chooser_min_channels() const {
    return recent_chooser_min_channels_;
  }

  td_api::object_ptr<td_api::messageReaction> get_message_reaction_object(Td *td, UserId my_user_id,
                                                                          UserId peer_user_id) const;

 public:
  MessageReaction() = default;

  const string &get_reaction() const {
    return reaction_;
  }

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const MessageReaction &lhs, const MessageReaction &rhs);

inline bool operator!=(const MessageReaction &lhs, const MessageReaction &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageReaction &reaction);

class UnreadMessageReaction {
  string reaction_;
  DialogId sender_dialog_id_;
  bool is_big_ = false;

  friend bool operator==(const UnreadMessageReaction &lhs, const UnreadMessageReaction &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const UnreadMessageReaction &message_reaction);

 public:
  UnreadMessageReaction() = default;

  UnreadMessageReaction(string reaction, DialogId sender_dialog_id, bool is_big)
      : reaction_(std::move(reaction)), sender_dialog_id_(sender_dialog_id), is_big_(is_big) {
  }

  td_api::object_ptr<td_api::unreadReaction> get_unread_reaction_object(Td *td) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const UnreadMessageReaction &lhs, const UnreadMessageReaction &rhs);

inline bool operator!=(const UnreadMessageReaction &lhs, const UnreadMessageReaction &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const UnreadMessageReaction &unread_reaction);

struct MessageReactions {
  vector<MessageReaction> reactions_;
  vector<UnreadMessageReaction> unread_reactions_;
  vector<string> chosen_reaction_order_;
  bool is_min_ = false;
  bool need_polling_ = true;
  bool can_get_added_reactions_ = false;

  MessageReactions() = default;

  static unique_ptr<MessageReactions> get_message_reactions(Td *td,
                                                            tl_object_ptr<telegram_api::messageReactions> &&reactions,
                                                            bool is_bot);

  MessageReaction *get_reaction(const string &reaction);

  const MessageReaction *get_reaction(const string &reaction) const;

  void update_from(const MessageReactions &old_reactions);

  bool add_reaction(const string &reaction, bool is_big, DialogId chooser_dialog_id, bool have_recent_choosers);

  bool remove_reaction(const string &reaction, DialogId chooser_dialog_id, bool have_recent_choosers);

  void sort_reactions(const FlatHashMap<string, size_t> &active_reaction_pos);

  void fix_chosen_reaction(DialogId my_dialog_id);

  vector<string> get_chosen_reactions() const;

  bool are_consistent_with_list(const string &reaction, FlatHashMap<string, vector<DialogId>> reactions,
                                int32 total_count) const;

  vector<td_api::object_ptr<td_api::messageReaction>> get_message_reactions_object(Td *td, UserId my_user_id,
                                                                                   UserId peer_user_id) const;

  void add_min_channels(Td *td) const;

  void add_dependencies(Dependencies &dependencies) const;

  static bool need_update_message_reactions(const MessageReactions *old_reactions,
                                            const MessageReactions *new_reactions);

  static bool need_update_unread_reactions(const MessageReactions *old_reactions,
                                           const MessageReactions *new_reactions);

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);

 private:
  bool do_remove_reaction(const string &reaction, DialogId chooser_dialog_id, bool have_recent_choosers);
};

StringBuilder &operator<<(StringBuilder &string_builder, const MessageReactions &reactions);

StringBuilder &operator<<(StringBuilder &string_builder, const unique_ptr<MessageReactions> &reactions);

telegram_api::object_ptr<telegram_api::Reaction> get_input_reaction(const string &reaction);

td_api::object_ptr<td_api::ReactionType> get_reaction_type_object(const string &reaction);

string get_message_reaction_string(const telegram_api::object_ptr<telegram_api::Reaction> &reaction);

string get_message_reaction_string(const td_api::object_ptr<td_api::ReactionType> &type);

bool is_custom_reaction(const string &reaction);

bool is_active_reaction(const string &reaction, const FlatHashMap<string, size_t> &active_reaction_pos);

void reload_message_reactions(Td *td, DialogId dialog_id, vector<MessageId> &&message_ids);

void send_message_reaction(Td *td, FullMessageId full_message_id, vector<string> reactions, bool is_big,
                           bool add_to_recent, Promise<Unit> &&promise);

void get_message_added_reactions(Td *td, FullMessageId full_message_id, string reaction, string offset, int32 limit,
                                 Promise<td_api::object_ptr<td_api::addedReactions>> &&promise);

void set_default_reaction(Td *td, string reaction, Promise<Unit> &&promise);

void send_set_default_reaction_query(Td *td);

td_api::object_ptr<td_api::updateDefaultReactionType> get_update_default_reaction_type(const string &default_reaction);

void report_message_reactions(Td *td, FullMessageId full_message_id, DialogId chooser_dialog_id,
                              Promise<Unit> &&promise);

vector<string> get_recent_reactions(Td *td);

vector<string> get_top_reactions(Td *td);

void add_recent_reaction(Td *td, const string &reaction);

int64 get_reactions_hash(const vector<string> &reactions);

}  // namespace td
