// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BotAccessSettings.h"

#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"

#include "td/utils/HashSet.h"
#include "td/utils/logging.h"

namespace td {

namespace {

UserId get_server_user_id(const telegram_api::object_ptr<telegram_api::User> &user) {
  CHECK(user != nullptr);
  switch (user->get_id()) {
    case telegram_api::userEmpty::ID:
      return UserId(static_cast<const telegram_api::userEmpty *>(user.get())->id_);
    case telegram_api::user::ID:
      return UserId(static_cast<const telegram_api::user *>(user.get())->id_);
    default:
      UNREACHABLE();
      return UserId();
  }
}

}  // namespace

BotAccessSettings::BotAccessSettings(Td *td, telegram_api::object_ptr<telegram_api::bots_accessSettings> &&settings) {
  CHECK(settings != nullptr);
  is_restricted_ = settings->restricted_;
  if (!is_restricted_ && !settings->add_users_.empty()) {
    validation_status_ = Status::Error(500, "Receive added users in a non-restricted bot");
    LOG(ERROR) << validation_status_;
    return;
  }

  if (is_restricted_) {
    for (auto &user : settings->add_users_) {
      auto user_id = get_server_user_id(user);
      if (!user_id.is_valid()) {
        validation_status_ = Status::Error(500, "Receive invalid added user in bot access settings");
        LOG(ERROR) << validation_status_;
        added_user_ids_.clear();
        return;
      }
      CHECK(td != nullptr);
      td->user_manager_->on_get_user(std::move(user), "BotAccessSettings");
      added_user_ids_.push_back(user_id);
    }
  }
}

BotAccessSettings::BotAccessSettings(td_api::object_ptr<td_api::botAccessSettings> &&settings) {
  if (settings == nullptr) {
    validation_status_ = Status::Error(400, "Managed bot access settings must be specified");
    return;
  }

  is_restricted_ = settings->is_restricted_;
  if (!is_restricted_ && !settings->added_user_ids_.empty()) {
    validation_status_ = Status::Error(400, "Added users can be specified only for restricted bot access settings");
    return;
  }

  FlatHashSet<int64> seen_user_ids;
  for (auto added_user_id : settings->added_user_ids_) {
    UserId user_id(added_user_id);
    if (!user_id.is_valid()) {
      validation_status_ = Status::Error(400, "Invalid added user identifier specified");
      added_user_ids_.clear();
      return;
    }
    if (seen_user_ids.insert(user_id.get()).second) {
      added_user_ids_.push_back(user_id);
    }
  }
}

td_api::object_ptr<td_api::botAccessSettings> BotAccessSettings::get_bot_access_settings_object(Td *td) const {
  return td_api::make_object<td_api::botAccessSettings>(
      is_restricted_, td->user_manager_->get_user_ids_object(added_user_ids_, "botAccessSettings"));
}

}  // namespace td
