//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/EmojiStatus.h"

#include "td/telegram/Global.h"
#include "td/telegram/Td.h"

#include "td/utils/buffer.h"
#include "td/utils/Status.h"

namespace td {

class GetDefaultEmojiStatusesQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::premiumStatuses>> promise_;

 public:
  explicit GetDefaultEmojiStatusesQuery(Promise<td_api::object_ptr<td_api::premiumStatuses>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::account_getDefaultEmojiStatuses(hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getDefaultEmojiStatuses>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto emoji_statuses_ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetDefaultEmojiStatusesQuery: " << to_string(emoji_statuses_ptr);

    auto result = td_api::make_object<td_api::premiumStatuses>();
    if (emoji_statuses_ptr->get_id() == telegram_api::account_emojiStatuses::ID) {
      auto emoji_statuses = move_tl_object_as<telegram_api::account_emojiStatuses>(emoji_statuses_ptr);
      for (auto &status : emoji_statuses->statuses_) {
        EmojiStatus emoji_status(std::move(status));
        if (emoji_status.is_empty()) {
          LOG(ERROR) << "Receive empty default emoji status";
          continue;
        }
        result->premium_statuses_.push_back(emoji_status.get_premium_status_object());
      }
    }
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

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

void get_default_emoji_statuses(Td *td, Promise<td_api::object_ptr<td_api::premiumStatuses>> &&promise) {
  td->create_handler<GetDefaultEmojiStatusesQuery>(std::move(promise))->send(0);
}

}  // namespace td
