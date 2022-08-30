//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

namespace td {

class EmojiStatus {
  int64 custom_emoji_id_ = 0;

  friend bool operator==(const EmojiStatus &lhs, const EmojiStatus &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const EmojiStatus &contact);

 public:
  EmojiStatus() = default;

  explicit EmojiStatus(int64 custom_emoji_id) : custom_emoji_id_(custom_emoji_id) {
  }

  explicit EmojiStatus(tl_object_ptr<telegram_api::EmojiStatus> &&emoji_status) {
    if (emoji_status != nullptr && emoji_status->get_id() == telegram_api::emojiStatus::ID) {
      custom_emoji_id_ = static_cast<const telegram_api::emojiStatus *>(emoji_status.get())->document_id_;
    }
  }

  tl_object_ptr<telegram_api::EmojiStatus> get_input_emoji_status() const {
    if (is_empty()) {
      return make_tl_object<telegram_api::emojiStatusEmpty>();
    }
    return make_tl_object<telegram_api::emojiStatus>(custom_emoji_id_);
  }

  int64 get_premium_badge_object() const {
    return custom_emoji_id_;
  }

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

inline StringBuilder &operator<<(StringBuilder &string_builder, const EmojiStatus &emoji_status) {
  if (emoji_status.is_empty()) {
    return string_builder << "DefaultProfileBadge";
  }
  return string_builder << "CustomEmoji " << emoji_status.custom_emoji_id_;
}

}  // namespace td
