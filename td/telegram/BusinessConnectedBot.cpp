//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BusinessConnectedBot.h"

#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"

namespace td {

BusinessConnectedBot::BusinessConnectedBot(telegram_api::object_ptr<telegram_api::connectedBot> connected_bot) {
  CHECK(connected_bot != nullptr);
  user_id_ = UserId(connected_bot->bot_id_);
  recipients_ = BusinessRecipients(std::move(connected_bot->recipients_));
  rights_ = BusinessBotRights(connected_bot->rights_);
  device_ = std::move(connected_bot->device_);
  location_ = std::move(connected_bot->location_);
  date_ = connected_bot->date_;
}

BusinessConnectedBot::BusinessConnectedBot(td_api::object_ptr<td_api::businessConnectedBot> connected_bot) {
  if (connected_bot == nullptr) {
    return;
  }
  user_id_ = UserId(connected_bot->bot_user_id_);
  recipients_ = BusinessRecipients(std::move(connected_bot->recipients_), true);
  rights_ = BusinessBotRights(connected_bot->rights_);
}

td_api::object_ptr<td_api::businessConnectedBot> BusinessConnectedBot::get_business_connected_bot_object(Td *td) const {
  CHECK(is_valid());
  return td_api::make_object<td_api::businessConnectedBot>(
      td->user_manager_->get_user_id_object(user_id_, "businessConnectedBot"),
      recipients_.get_business_recipients_object(td), rights_.get_business_bot_rights_object());
}

td_api::object_ptr<td_api::businessConnectedBotInfo> BusinessConnectedBot::get_business_connected_bot_info_object(
    Td *td) const {
  return td_api::make_object<td_api::businessConnectedBotInfo>(get_business_connected_bot_object(td), date_, device_,
                                                               location_);
}

bool operator==(const BusinessConnectedBot &lhs, const BusinessConnectedBot &rhs) {
  return lhs.user_id_ == rhs.user_id_ && lhs.recipients_ == rhs.recipients_ && lhs.rights_ == rhs.rights_ &&
         lhs.device_ == rhs.device_ && lhs.location_ == rhs.location_ && lhs.date_ == rhs.date_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const BusinessConnectedBot &connected_bot) {
  return string_builder << "connected bot " << connected_bot.user_id_ << ' ' << connected_bot.recipients_ << " with "
                        << connected_bot.rights_;
}

}  // namespace td
