//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

namespace td {

class Td;

class EmojiStatus {
  int64 custom_emoji_id_ = 0;

  friend bool operator==(const EmojiStatus &lhs, const EmojiStatus &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const EmojiStatus &contact);

 public:
  EmojiStatus() = default;

  explicit EmojiStatus(const td_api::object_ptr<td_api::premiumStatus> &premium_status);

  explicit EmojiStatus(tl_object_ptr<telegram_api::EmojiStatus> &&emoji_status);

  tl_object_ptr<telegram_api::EmojiStatus> get_input_emoji_status() const;

  td_api::object_ptr<td_api::premiumStatus> get_premium_status_object() const;

  bool is_empty() const {
    return custom_emoji_id_ == 0;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(custom_emoji_id_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(custom_emoji_id_, parser);
  }
};

inline bool operator==(const EmojiStatus &lhs, const EmojiStatus &rhs) {
  return lhs.custom_emoji_id_ == rhs.custom_emoji_id_;
}

inline bool operator!=(const EmojiStatus &lhs, const EmojiStatus &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const EmojiStatus &emoji_status);

void get_default_emoji_statuses(Td *td, Promise<td_api::object_ptr<td_api::premiumStatuses>> &&promise);

void get_recent_emoji_statuses(Td *td, Promise<td_api::object_ptr<td_api::premiumStatuses>> &&promise);

}  // namespace td
