//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
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

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/StringBuilder.h"

#include <utility>

namespace td {

class Td;

class MessageReaction {
  string reaction_;
  int32 choose_count_ = 0;
  bool is_chosen_ = false;
  vector<DialogId> recent_chooser_dialog_ids_;
  vector<std::pair<ChannelId, MinChannel>> recent_chooser_min_channels_;

  friend bool operator==(const MessageReaction &lhs, const MessageReaction &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const MessageReaction &message_reaction);

 public:
  static constexpr size_t MAX_RECENT_CHOOSERS = 3;
  static constexpr int32 MAX_CHOOSE_COUNT = 2147483640;

  MessageReaction() = default;

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

  const string &get_reaction() const {
    return reaction_;
  }

  bool is_chosen() const {
    return is_chosen_;
  }

  void set_is_chosen(bool is_chosen, DialogId chooser_dialog_id, bool can_get_added_reactions);

  int32 get_choose_count() const {
    return choose_count_;
  }

  const vector<DialogId> &get_recent_chooser_dialog_ids() const {
    return recent_chooser_dialog_ids_;
  }

  const vector<std::pair<ChannelId, MinChannel>> &get_recent_chooser_min_channels() const {
    return recent_chooser_min_channels_;
  }

  void add_recent_chooser_dialog_id(DialogId dialog_id);

  bool remove_recent_chooser_dialog_id(DialogId dialog_id);

  td_api::object_ptr<td_api::messageReaction> get_message_reaction_object(Td *td) const;

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

  void sort_reactions(const FlatHashMap<string, size_t> &active_reaction_pos);

  void fix_chosen_reaction(DialogId my_dialog_id);

  static bool need_update_message_reactions(const MessageReactions *old_reactions,
                                            const MessageReactions *new_reactions);

  static bool need_update_unread_reactions(const MessageReactions *old_reactions,
                                           const MessageReactions *new_reactions);

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

StringBuilder &operator<<(StringBuilder &string_builder, const MessageReactions &reactions);

StringBuilder &operator<<(StringBuilder &string_builder, const unique_ptr<MessageReactions> &reactions);

void reload_message_reactions(Td *td, DialogId dialog_id, vector<MessageId> &&message_ids);

void set_message_reaction(Td *td, FullMessageId full_message_id, string reaction, bool is_big, Promise<Unit> &&promise);

void get_message_added_reactions(Td *td, FullMessageId full_message_id, string reaction, string offset, int32 limit,
                                 Promise<td_api::object_ptr<td_api::addedReactions>> &&promise);

}  // namespace td
