//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BotCommand.h"

#include "td/telegram/BotCommandScope.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"
#include "td/utils/utf8.h"

namespace td {

class SetBotCommandsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetBotCommandsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(BotCommandScope scope, const string &language_code, vector<BotCommand> &&commands) {
    send_query(G()->net_query_creator().create(telegram_api::bots_setBotCommands(
        scope.get_input_bot_command_scope(td), language_code,
        transform(commands, [](const BotCommand &command) { return command.get_input_bot_command(); }))));
  }

  void on_result(uint64 id, BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_setBotCommands>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    if (!result_ptr.ok()) {
      LOG(ERROR) << "Set bot commands request failed";
    }
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ResetBotCommandsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ResetBotCommandsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(BotCommandScope scope, const string &language_code) {
    send_query(G()->net_query_creator().create(
        telegram_api::bots_resetBotCommands(scope.get_input_bot_command_scope(td), language_code)));
  }

  void on_result(uint64 id, BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_resetBotCommands>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetBotCommandsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::botCommands>> promise_;

 public:
  explicit GetBotCommandsQuery(Promise<td_api::object_ptr<td_api::botCommands>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(BotCommandScope scope, const string &language_code) {
    send_query(G()->net_query_creator().create(
        telegram_api::bots_getBotCommands(scope.get_input_bot_command_scope(td), language_code)));
  }

  void on_result(uint64 id, BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_getBotCommands>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    BotCommands commands(td->contacts_manager_->get_my_id(), result_ptr.move_as_ok());
    promise_.set_value(commands.get_bot_commands_object(td));
  }

  void on_error(uint64 id, Status status) final {
    promise_.set_error(std::move(status));
  }
};

BotCommand::BotCommand(telegram_api::object_ptr<telegram_api::botCommand> &&bot_command) {
  CHECK(bot_command != nullptr);
  command_ = std::move(bot_command->command_);
  description_ = std::move(bot_command->description_);
}

td_api::object_ptr<td_api::botCommand> BotCommand::get_bot_command_object() const {
  return td_api::make_object<td_api::botCommand>(command_, description_);
}

telegram_api::object_ptr<telegram_api::botCommand> BotCommand::get_input_bot_command() const {
  return telegram_api::make_object<telegram_api::botCommand>(command_, description_);
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

static bool is_valid_language_code(const string &language_code) {
  if (language_code.empty()) {
    return true;
  }
  if (language_code.size() != 2) {
    return false;
  }
  return 'a' <= language_code[0] && language_code[0] <= 'z' && 'a' <= language_code[1] && language_code[1] <= 'z';
}

void set_commands(Td *td, td_api::object_ptr<td_api::BotCommandScope> &&scope_ptr, string &&language_code,
                  vector<td_api::object_ptr<td_api::botCommand>> &&commands, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, scope, BotCommandScope::get_bot_command_scope(td, std::move(scope_ptr)));

  if (!is_valid_language_code(language_code)) {
    return promise.set_error(Status::Error(400, "Invalid language code specified"));
  }

  vector<BotCommand> new_commands;
  for (auto &command : commands) {
    if (command == nullptr) {
      return promise.set_error(Status::Error(400, "Command must be non-empty"));
    }
    if (!clean_input_string(command->command_)) {
      return promise.set_error(Status::Error(400, "Command must be encoded in UTF-8"));
    }
    if (!clean_input_string(command->description_)) {
      return promise.set_error(Status::Error(400, "Command description must be encoded in UTF-8"));
    }

    const size_t MAX_COMMAND_TEXT_LENGTH = 32;
    command->command_ = trim(command->command_);
    if (command->command_[0] == '/') {
      command->command_ = command->command_.substr(1);
    }
    if (command->command_.empty()) {
      return promise.set_error(Status::Error(400, "Command must be non-empty"));
    }
    if (utf8_length(command->command_) > MAX_COMMAND_TEXT_LENGTH) {
      return promise.set_error(
          Status::Error(400, PSLICE() << "Command length must not exceed " << MAX_COMMAND_TEXT_LENGTH));
    }

    const size_t MAX_COMMAND_DESCRIPTION_LENGTH = 256;
    command->description_ = trim(command->description_);
    auto description_length = utf8_length(command->description_);
    if (command->description_.empty()) {
      return promise.set_error(Status::Error(400, "Command description must be non-empty"));
    }
    if (description_length > MAX_COMMAND_DESCRIPTION_LENGTH) {
      return promise.set_error(Status::Error(
          400, PSLICE() << "Command description length must not exceed " << MAX_COMMAND_DESCRIPTION_LENGTH));
    }

    new_commands.emplace_back(std::move(command->command_), std::move(command->description_));
  }

  td->create_handler<SetBotCommandsQuery>(std::move(promise))->send(scope, language_code, std::move(new_commands));
}

void delete_commands(Td *td, td_api::object_ptr<td_api::BotCommandScope> &&scope_ptr, string &&language_code,
                     Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, scope, BotCommandScope::get_bot_command_scope(td, std::move(scope_ptr)));

  if (!is_valid_language_code(language_code)) {
    return promise.set_error(Status::Error(400, "Invalid language code specified"));
  }

  td->create_handler<ResetBotCommandsQuery>(std::move(promise))->send(scope, language_code);
}

void get_commands(Td *td, td_api::object_ptr<td_api::BotCommandScope> &&scope_ptr, string &&language_code,
                  Promise<td_api::object_ptr<td_api::botCommands>> &&promise) {
  TRY_RESULT_PROMISE(promise, scope, BotCommandScope::get_bot_command_scope(td, std::move(scope_ptr)));

  if (!is_valid_language_code(language_code)) {
    return promise.set_error(Status::Error(400, "Invalid language code specified"));
  }

  td->create_handler<GetBotCommandsQuery>(std::move(promise))->send(scope, language_code);
}

}  // namespace td
