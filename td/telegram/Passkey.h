//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class Passkey {
  string id_;
  string name_;
  int32 added_date_ = 0;
  int32 last_usage_date_ = 0;
  CustomEmojiId software_custom_emoji_id_;

 public:
  explicit Passkey(telegram_api::object_ptr<telegram_api::passkey> &&passkey);

  td_api::object_ptr<td_api::passkey> get_passkey_object() const;
};

}  // namespace td
