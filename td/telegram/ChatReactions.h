//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

namespace td {

struct ChatReactions {
  vector<string> reactions_;
  bool allow_all_ = false;     // implies empty reactions
  bool allow_custom_ = false;  // implies allow_all

  ChatReactions() = default;

  explicit ChatReactions(vector<string> &&reactions) : reactions_(std::move(reactions)) {
  }

  explicit ChatReactions(telegram_api::object_ptr<telegram_api::ChatReactions> &&chat_reactions_ptr);

  ChatReactions(td_api::object_ptr<td_api::ChatAvailableReactions> &&chat_reactions_ptr, bool allow_custom);

  ChatReactions(bool allow_all, bool allow_custom) : allow_all_(allow_all), allow_custom_(allow_custom) {
  }

  ChatReactions get_active_reactions(const FlatHashMap<string, size_t> &active_reaction_pos) const;

  bool is_allowed_reaction(const string &reaction) const;

  telegram_api::object_ptr<telegram_api::ChatReactions> get_input_chat_reactions() const;

  td_api::object_ptr<td_api::ChatAvailableReactions> get_chat_available_reactions_object() const;

  bool empty() const {
    return reactions_.empty() && !allow_all_;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    bool has_reactions = !reactions_.empty();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(allow_all_);
    STORE_FLAG(allow_custom_);
    STORE_FLAG(has_reactions);
    END_STORE_FLAGS();
    if (has_reactions) {
      td::store(reactions_, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    bool has_reactions;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(allow_all_);
    PARSE_FLAG(allow_custom_);
    PARSE_FLAG(has_reactions);
    END_PARSE_FLAGS();
    if (has_reactions) {
      td::parse(reactions_, parser);
    }
  }
};

bool operator==(const ChatReactions &lhs, const ChatReactions &rhs);

inline bool operator!=(const ChatReactions &lhs, const ChatReactions &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const ChatReactions &reactions);

}  // namespace td
