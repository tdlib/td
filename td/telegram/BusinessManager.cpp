//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BusinessManager.h"

#include "td/telegram/BusinessAwayMessage.h"
#include "td/telegram/BusinessConnectedBot.h"
#include "td/telegram/BusinessGreetingMessage.h"
#include "td/telegram/BusinessRecipients.h"
#include "td/telegram/BusinessWorkHours.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogLocation.h"
#include "td/telegram/Global.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

namespace td {

class GetConnectedBotsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::businessConnectedBot>> promise_;

 public:
  explicit GetConnectedBotsQuery(Promise<td_api::object_ptr<td_api::businessConnectedBot>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_getConnectedBots(), {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getConnectedBots>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetConnectedBotsQuery: " << to_string(result);

    td_->contacts_manager_->on_get_users(std::move(result->users_), "GetConnectedBotsQuery");
    if (result->connected_bots_.size() > 1u) {
      return on_error(Status::Error(500, "Receive invalid response"));
    }
    if (result->connected_bots_.empty()) {
      return promise_.set_value(nullptr);
    }
    auto bot = BusinessConnectedBot(std::move(result->connected_bots_[0]));
    if (!bot.is_valid()) {
      return on_error(Status::Error(500, "Receive invalid bot"));
    }
    promise_.set_value(bot.get_business_connected_bot_object(td_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class UpdateConnectedBotQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit UpdateConnectedBotQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const BusinessConnectedBot &bot, telegram_api::object_ptr<telegram_api::InputUser> &&input_user) {
    int32 flags = 0;
    if (bot.get_can_reply()) {
      flags |= telegram_api::account_updateConnectedBot::CAN_REPLY_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::account_updateConnectedBot(flags, false /*ignored*/, false /*ignored*/, std::move(input_user),
                                                 bot.get_recipients().get_input_business_recipients(td_)),
        {{"me"}}));
  }

  void send(telegram_api::object_ptr<telegram_api::InputUser> &&input_user) {
    int32 flags = telegram_api::account_updateConnectedBot::DELETED_MASK;
    send_query(G()->net_query_creator().create(
        telegram_api::account_updateConnectedBot(flags, false /*ignored*/, false /*ignored*/, std::move(input_user),
                                                 BusinessRecipients().get_input_business_recipients(td_)),
        {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_updateConnectedBot>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for UpdateConnectedBotQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class UpdateBusinessLocationQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogLocation location_;

 public:
  explicit UpdateBusinessLocationQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogLocation &&location) {
    location_ = std::move(location);
    int32 flags = 0;
    if (!location_.empty()) {
      flags |= telegram_api::account_updateBusinessLocation::GEO_POINT_MASK;
    }
    if (!location_.get_address().empty()) {
      flags |= telegram_api::account_updateBusinessLocation::ADDRESS_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::account_updateBusinessLocation(flags, location_.get_input_geo_point(), location_.get_address()),
        {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_updateBusinessLocation>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->contacts_manager_->on_update_user_location(td_->contacts_manager_->get_my_id(), std::move(location_));

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class UpdateBusinessWorkHoursQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  BusinessWorkHours work_hours_;

 public:
  explicit UpdateBusinessWorkHoursQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(BusinessWorkHours &&work_hours) {
    work_hours_ = std::move(work_hours);
    int32 flags = 0;
    if (!work_hours_.is_empty()) {
      flags |= telegram_api::account_updateBusinessWorkHours::BUSINESS_WORK_HOURS_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::account_updateBusinessWorkHours(flags, work_hours_.get_input_business_work_hours()), {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_updateBusinessWorkHours>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->contacts_manager_->on_update_user_work_hours(td_->contacts_manager_->get_my_id(), std::move(work_hours_));

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class UpdateBusinessGreetingMessageQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  BusinessGreetingMessage greeting_message_;

 public:
  explicit UpdateBusinessGreetingMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(BusinessGreetingMessage &&greeting_message) {
    greeting_message_ = std::move(greeting_message);
    int32 flags = 0;
    if (!greeting_message_.is_empty()) {
      flags |= telegram_api::account_updateBusinessGreetingMessage::MESSAGE_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::account_updateBusinessGreetingMessage(
                                                   flags, greeting_message_.get_input_business_greeting_message(td_)),
                                               {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_updateBusinessGreetingMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->contacts_manager_->on_update_user_greeting_message(td_->contacts_manager_->get_my_id(),
                                                            std::move(greeting_message_));

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class UpdateBusinessAwayMessageQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  BusinessAwayMessage away_message_;

 public:
  explicit UpdateBusinessAwayMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(BusinessAwayMessage &&away_message) {
    away_message_ = std::move(away_message);
    int32 flags = 0;
    if (!away_message_.is_empty()) {
      flags |= telegram_api::account_updateBusinessAwayMessage::MESSAGE_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::account_updateBusinessAwayMessage(flags, away_message_.get_input_business_away_message(td_)),
        {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_updateBusinessAwayMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->contacts_manager_->on_update_user_away_message(td_->contacts_manager_->get_my_id(), std::move(away_message_));

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

BusinessManager::BusinessManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void BusinessManager::tear_down() {
  parent_.reset();
}

void BusinessManager::get_business_connected_bot(Promise<td_api::object_ptr<td_api::businessConnectedBot>> &&promise) {
  td_->create_handler<GetConnectedBotsQuery>(std::move(promise))->send();
}

void BusinessManager::set_business_connected_bot(td_api::object_ptr<td_api::businessConnectedBot> &&bot,
                                                 Promise<Unit> &&promise) {
  if (bot == nullptr) {
    return promise.set_error(Status::Error(400, "Bot must be non-empty"));
  }
  BusinessConnectedBot connected_bot(std::move(bot));
  TRY_RESULT_PROMISE(promise, input_user, td_->contacts_manager_->get_input_user(connected_bot.get_user_id()));
  td_->create_handler<UpdateConnectedBotQuery>(std::move(promise))->send(connected_bot, std::move(input_user));
}

void BusinessManager::delete_business_connected_bot(UserId bot_user_id, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_user, td_->contacts_manager_->get_input_user(bot_user_id));
  td_->create_handler<UpdateConnectedBotQuery>(std::move(promise))->send(std::move(input_user));
}

void BusinessManager::set_business_location(DialogLocation &&location, Promise<Unit> &&promise) {
  td_->create_handler<UpdateBusinessLocationQuery>(std::move(promise))->send(std::move(location));
}

void BusinessManager::set_business_work_hours(BusinessWorkHours &&work_hours, Promise<Unit> &&promise) {
  td_->create_handler<UpdateBusinessWorkHoursQuery>(std::move(promise))->send(std::move(work_hours));
}

void BusinessManager::set_business_greeting_message(BusinessGreetingMessage &&greeting_message,
                                                    Promise<Unit> &&promise) {
  td_->create_handler<UpdateBusinessGreetingMessageQuery>(std::move(promise))->send(std::move(greeting_message));
}

void BusinessManager::set_business_away_message(BusinessAwayMessage &&away_message, Promise<Unit> &&promise) {
  td_->create_handler<UpdateBusinessAwayMessageQuery>(std::move(promise))->send(std::move(away_message));
}

}  // namespace td
