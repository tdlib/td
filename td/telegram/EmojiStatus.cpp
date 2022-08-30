//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/EmojiStatus.h"

namespace td {

EmojiStatus::EmojiStatus(const td_api::object_ptr<td_api::premiumStatus> &premium_status)
    : custom_emoji_id_(premium_status != nullptr ? premium_status->custom_emoji_id_ : 0) {
}

EmojiStatus::EmojiStatus(tl_object_ptr<telegram_api::EmojiStatus> &&emoji_status) {
  if (emoji_status != nullptr && emoji_status->get_id() == telegram_api::emojiStatus::ID) {
    custom_emoji_id_ = static_cast<const telegram_api::emojiStatus *>(emoji_status.get())->document_id_;
  }
}

tl_object_ptr<telegram_api::EmojiStatus> EmojiStatus::get_input_emoji_status() const {
  if (is_empty()) {
    return make_tl_object<telegram_api::emojiStatusEmpty>();
  }
  return make_tl_object<telegram_api::emojiStatus>(custom_emoji_id_);
}

td_api::object_ptr<td_api::premiumStatus> EmojiStatus::get_premium_status_object() const {
  if (is_empty()) {
    return nullptr;
  }
  return td_api::make_object<td_api::premiumStatus>(custom_emoji_id_);
}

StringBuilder &operator<<(StringBuilder &string_builder, const EmojiStatus &emoji_status) {
  if (emoji_status.is_empty()) {
    return string_builder << "DefaultProfileBadge";
  }
  return string_builder << "CustomEmoji " << emoji_status.custom_emoji_id_;
}

}  // namespace td
