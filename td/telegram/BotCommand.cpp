//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BotCommand.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"

namespace td {

BotCommand::BotCommand(telegram_api::object_ptr<telegram_api::botCommand> &&bot_command) {
  CHECK(bot_command != nullptr);
  command_ = std::move(bot_command->command_);
  description_ = std::move(bot_command->description_);
}

td_api::object_ptr<td_api::botCommand> BotCommand::get_bot_command_object() const {
  return td_api::make_object<td_api::botCommand>(command_, description_);
}

bool operator==(const BotCommand &lhs, const BotCommand &rhs) {
  return lhs.command_ == rhs.command_ && lhs.description_ == rhs.description_;
}

BotCommands::BotCommands(UserId bot_user_id, vector<telegram_api::object_ptr<telegram_api::botCommand>> &&bot_commands)
    : bot_user_id_(bot_user_id) {
  commands_ = transform(std::move(bot_commands), [](telegram_api::object_ptr<telegram_api::botCommand> &&bot_command) {
    return BotCommand(std::move(bot_command));
  });
}

td_api::object_ptr<td_api::botCommands> BotCommands::get_bot_commands_object(Td *td) const {
  auto commands = transform(commands_, [](const auto &command) { return command.get_bot_command_object(); });
  return td_api::make_object<td_api::botCommands>(
      td->contacts_manager_->get_user_id_object(bot_user_id_, "get_bot_commands_object"), std::move(commands));
}

bool operator==(const BotCommands &lhs, const BotCommands &rhs) {
  return lhs.bot_user_id_ == rhs.bot_user_id_ && lhs.commands_ == rhs.commands_;
}

}  // namespace td
