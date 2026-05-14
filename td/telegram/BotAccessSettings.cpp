//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BotAccessSettings.h"

#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"

#include "td/utils/logging.h"

namespace td {

BotAccessSettings::BotAccessSettings(Td *td, telegram_api::object_ptr<telegram_api::bots_accessSettings> &&settings) {
  CHECK(settings != nullptr);
  is_restricted_ = settings->restricted_;
  if (is_restricted_) {
    for (auto &user : settings->add_users_) {
      auto user_id = UserManager::get_user_id(user);
      if (user_id.is_valid()) {
        td->user_manager_->on_get_user(std::move(user), "BotAccessSettings");
        added_user_ids_.push_back(user_id);
      }
    }
  } else if (!settings->add_users_.empty()) {
    LOG(ERROR) << "Receive added users in a non-restricted bot";
  }
}

BotAccessSettings::BotAccessSettings(td_api::object_ptr<td_api::botAccessSettings> &&settings) {
  if (settings != nullptr) {
    is_restricted_ = settings->is_restricted_;
    for (auto added_user_id : settings->added_user_ids_) {
      UserId user_id(added_user_id);
      if (user_id.is_valid()) {
        added_user_ids_.push_back(user_id);
      }
    }
  }
}

td_api::object_ptr<td_api::botAccessSettings> BotAccessSettings::get_bot_access_settings_object(Td *td) const {
  return td_api::make_object<td_api::botAccessSettings>(
      is_restricted_, td->user_manager_->get_user_ids_object(added_user_ids_, "botAccessSettings"));
}

}  // namespace td
