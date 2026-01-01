//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Passkey.h"

#include "td/utils/logging.h"

namespace td {

Passkey::Passkey(telegram_api::object_ptr<telegram_api::passkey> &&passkey)
    : id_(std::move(passkey->id_))
    , name_(std::move(passkey->name_))
    , added_date_(passkey->date_)
    , last_usage_date_(passkey->last_usage_date_)
    , software_custom_emoji_id_(passkey->software_emoji_id_) {
  if (!software_custom_emoji_id_.is_valid() && software_custom_emoji_id_ != CustomEmojiId()) {
    LOG(ERROR) << "Receive " << software_custom_emoji_id_;
    software_custom_emoji_id_ = CustomEmojiId();
  }
}

td_api::object_ptr<td_api::passkey> Passkey::get_passkey_object() const {
  return td_api::make_object<td_api::passkey>(id_, name_, added_date_, last_usage_date_,
                                              software_custom_emoji_id_.get());
}

}  // namespace td
