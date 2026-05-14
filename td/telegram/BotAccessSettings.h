//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"

namespace td {

class Td;

class BotAccessSettings {
  bool is_restricted_ = false;
  vector<UserId> added_user_ids_;

 public:
  BotAccessSettings(Td *td, telegram_api::object_ptr<telegram_api::bots_accessSettings> &&settings);

  explicit BotAccessSettings(td_api::object_ptr<td_api::botAccessSettings> &&settings);

  td_api::object_ptr<td_api::botAccessSettings> get_bot_access_settings_object(Td *td) const;

  bool is_restricted() const {
    return is_restricted_;
  }

  const vector<UserId> &get_added_user_ids() const {
    return added_user_ids_;
  }
};

}  // namespace td
