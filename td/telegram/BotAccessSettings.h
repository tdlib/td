// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class BotAccessSettings {
  bool is_restricted_ = false;
  vector<UserId> added_user_ids_;
  Status validation_status_;

 public:
  BotAccessSettings(Td *td, telegram_api::object_ptr<telegram_api::bots_accessSettings> &&settings);

  explicit BotAccessSettings(td_api::object_ptr<td_api::botAccessSettings> &&settings);

  td_api::object_ptr<td_api::botAccessSettings> get_bot_access_settings_object(Td *td) const;

  Status get_validation_status() const {
    return validation_status_.clone();
  }

  bool is_restricted() const {
    return is_restricted_;
  }

  const vector<UserId> &get_added_user_ids() const {
    return added_user_ids_;
  }

  template <class ResolveInputUser>
  Result<vector<telegram_api::object_ptr<telegram_api::InputUser>>> resolve_added_input_users(
      ResolveInputUser &&resolve_input_user) const {
    if (validation_status_.is_error()) {
      return validation_status_.clone();
    }

    vector<telegram_api::object_ptr<telegram_api::InputUser>> input_users;
    input_users.reserve(added_user_ids_.size());
    for (auto user_id : added_user_ids_) {
      auto r_input_user = resolve_input_user(user_id);
      if (r_input_user.is_error()) {
        return r_input_user.move_as_error();
      }
      input_users.push_back(r_input_user.move_as_ok());
    }
    return input_users;
  }
};

}  // namespace td
