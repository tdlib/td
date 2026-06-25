// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BusinessConnectedBot.h"
#include "td/telegram/BusinessConnectedBot.hpp"
#include "td/telegram/telegram_api.h"

#include "td/utils/tests.h"

namespace {

td::telegram_api::object_ptr<td::telegram_api::connectedBot> make_connected_bot(std::string device,
                                                                                td::int32 date,
                                                                                std::string location) {
  auto bot = td::telegram_api::make_object<td::telegram_api::connectedBot>();
  bot->flags_ = td::telegram_api::connectedBot::DEVICE_MASK | td::telegram_api::connectedBot::DATE_MASK |
                td::telegram_api::connectedBot::LOCATION_MASK;
  bot->bot_id_ = 777003;
  bot->recipients_ = td::telegram_api::make_object<td::telegram_api::businessBotRecipients>();
  bot->rights_ = td::telegram_api::make_object<td::telegram_api::businessBotRights>();
  bot->device_ = std::move(device);
  bot->date_ = date;
  bot->location_ = std::move(location);
  return bot;
}

TEST(BusinessConnectedBotInfoUnit, EqualityDependsOnLocation) {
  auto lhs = td::BusinessConnectedBot(make_connected_bot("device", 1712222222, "Tbilisi"));
  auto rhs = td::BusinessConnectedBot(make_connected_bot("device", 1712222222, "Rustavi"));

  ASSERT_NE(lhs, rhs);
}

TEST(BusinessConnectedBotInfoUnit, EqualityMatchesWhenAllFieldsMatch) {
  auto lhs = td::BusinessConnectedBot(make_connected_bot("device", 1712222222, "Tbilisi"));
  auto rhs = td::BusinessConnectedBot(make_connected_bot("device", 1712222222, "Tbilisi"));

  ASSERT_EQ(lhs, rhs);
}

}  // namespace
