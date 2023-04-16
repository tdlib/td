//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BotInfoManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

namespace td {

class SetBotGroupDefaultAdminRightsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetBotGroupDefaultAdminRightsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(AdministratorRights administrator_rights) {
    send_query(G()->net_query_creator().create(
        telegram_api::bots_setBotGroupDefaultAdminRights(administrator_rights.get_chat_admin_rights()), {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_setBotGroupDefaultAdminRights>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to set group default administrator rights";
    td_->contacts_manager_->invalidate_user_full(td_->contacts_manager_->get_my_id());
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    if (status.message() == "RIGHTS_NOT_MODIFIED") {
      return promise_.set_value(Unit());
    }
    td_->contacts_manager_->invalidate_user_full(td_->contacts_manager_->get_my_id());
    promise_.set_error(std::move(status));
  }
};

class SetBotBroadcastDefaultAdminRightsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SetBotBroadcastDefaultAdminRightsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(AdministratorRights administrator_rights) {
    send_query(G()->net_query_creator().create(
        telegram_api::bots_setBotBroadcastDefaultAdminRights(administrator_rights.get_chat_admin_rights()), {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_setBotBroadcastDefaultAdminRights>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to set channel default administrator rights";
    td_->contacts_manager_->invalidate_user_full(td_->contacts_manager_->get_my_id());
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    if (status.message() == "RIGHTS_NOT_MODIFIED") {
      return promise_.set_value(Unit());
    }
    td_->contacts_manager_->invalidate_user_full(td_->contacts_manager_->get_my_id());
    promise_.set_error(std::move(status));
  }
};

static Result<telegram_api::object_ptr<telegram_api::InputUser>> get_bot_input_user(const Td *td, UserId bot_user_id) {
  if (td->auth_manager_->is_bot()) {
    if (bot_user_id != UserId() && bot_user_id != td->contacts_manager_->get_my_id()) {
      return Status::Error(400, "Invalid bot user identifier specified");
    }
  } else {
    TRY_RESULT(bot_data, td->contacts_manager_->get_bot_data(bot_user_id));
    if (!bot_data.can_be_edited) {
      return Status::Error(400, "The bot can't be edited");
    }
    return td->contacts_manager_->get_input_user(bot_user_id);
  }
  return nullptr;
}

class SetBotInfoQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  UserId bot_user_id_;
  bool set_name_ = false;

  void invalidate_bot_info() {
    if (!set_name_) {
      td_->contacts_manager_->invalidate_user_full(bot_user_id_);
    }
  }

 public:
  explicit SetBotInfoQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(UserId bot_user_id, const string &language_code, bool set_name, const string &name, bool set_about,
            const string &about, bool set_description, const string &description) {
    int32 flags = 0;
    if (set_name) {
      flags |= telegram_api::bots_setBotInfo::NAME_MASK;
    }
    if (set_about) {
      flags |= telegram_api::bots_setBotInfo::ABOUT_MASK;
    }
    if (set_description) {
      flags |= telegram_api::bots_setBotInfo::DESCRIPTION_MASK;
    }
    auto r_input_user = get_bot_input_user(td_, bot_user_id);
    if (r_input_user.is_error()) {
      return on_error(r_input_user.move_as_error());
    }
    if (r_input_user.ok() != nullptr) {
      flags |= telegram_api::bots_setBotInfo::BOT_MASK;
      bot_user_id_ = bot_user_id;
    } else {
      bot_user_id_ = td_->contacts_manager_->get_my_id();
    }
    set_name_ = set_name;
    invalidate_bot_info();
    send_query(G()->net_query_creator().create(
        telegram_api::bots_setBotInfo(flags, r_input_user.move_as_ok(), language_code, name, about, description),
        {{bot_user_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_setBotInfo>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(WARNING, !result) << "Failed to set bot info";
    if (set_name_) {
      td_->contacts_manager_->reload_user(bot_user_id_, std::move(promise_));
    } else {
      invalidate_bot_info();
      if (td_->auth_manager_->is_bot()) {
        // invalidation is enough for bots
        promise_.set_value(Unit());
      } else {
        td_->contacts_manager_->reload_user_full(bot_user_id_, std::move(promise_));
      }
    }
  }

  void on_error(Status status) final {
    invalidate_bot_info();
    promise_.set_error(std::move(status));
  }
};

class GetBotInfoQuery final : public Td::ResultHandler {
  Promise<string> promise_;
  size_t index_ = 0;

 public:
  explicit GetBotInfoQuery(Promise<string> &&promise) : promise_(std::move(promise)) {
  }

  void send(UserId bot_user_id, const string &language_code, size_t index) {
    index_ = index;
    int32 flags = 0;
    auto r_input_user = get_bot_input_user(td_, bot_user_id);
    if (r_input_user.is_error()) {
      return on_error(r_input_user.move_as_error());
    }
    if (r_input_user.ok() != nullptr) {
      flags |= telegram_api::bots_getBotInfo::BOT_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::bots_getBotInfo(flags, r_input_user.move_as_ok(), language_code), {{bot_user_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_getBotInfo>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    switch (index_) {
      case 0:
        return promise_.set_value(std::move(result->about_));
      case 1:
        return promise_.set_value(std::move(result->description_));
      case 2:
        return promise_.set_value(std::move(result->name_));
      default:
        UNREACHABLE();
    }
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

BotInfoManager::BotInfoManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void BotInfoManager::tear_down() {
  parent_.reset();
}

void BotInfoManager::set_default_group_administrator_rights(AdministratorRights administrator_rights,
                                                            Promise<Unit> &&promise) {
  td_->contacts_manager_->invalidate_user_full(td_->contacts_manager_->get_my_id());
  td_->create_handler<SetBotGroupDefaultAdminRightsQuery>(std::move(promise))->send(administrator_rights);
}

void BotInfoManager::set_default_channel_administrator_rights(AdministratorRights administrator_rights,
                                                              Promise<Unit> &&promise) {
  td_->contacts_manager_->invalidate_user_full(td_->contacts_manager_->get_my_id());
  td_->create_handler<SetBotBroadcastDefaultAdminRightsQuery>(std::move(promise))->send(administrator_rights);
}

void BotInfoManager::set_bot_name(UserId bot_user_id, const string &language_code, const string &name,
                                  Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, validate_bot_language_code(language_code));
  td_->create_handler<SetBotInfoQuery>(std::move(promise))
      ->send(bot_user_id, language_code, true, name, false, string(), false, string());
}

void BotInfoManager::get_bot_name(UserId bot_user_id, const string &language_code, Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, validate_bot_language_code(language_code));
  td_->create_handler<GetBotInfoQuery>(std::move(promise))->send(bot_user_id, language_code, 2);
}

void BotInfoManager::set_bot_info_description(UserId bot_user_id, const string &language_code,
                                              const string &description, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, validate_bot_language_code(language_code));
  td_->create_handler<SetBotInfoQuery>(std::move(promise))
      ->send(bot_user_id, language_code, false, string(), false, string(), true, description);
}

void BotInfoManager::get_bot_info_description(UserId bot_user_id, const string &language_code,
                                              Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, validate_bot_language_code(language_code));
  td_->create_handler<GetBotInfoQuery>(std::move(promise))->send(bot_user_id, language_code, 1);
}

void BotInfoManager::set_bot_info_about(UserId bot_user_id, const string &language_code, const string &about,
                                        Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, validate_bot_language_code(language_code));
  td_->create_handler<SetBotInfoQuery>(std::move(promise))
      ->send(bot_user_id, language_code, false, string(), true, about, false, string());
}

void BotInfoManager::get_bot_info_about(UserId bot_user_id, const string &language_code, Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, validate_bot_language_code(language_code));
  td_->create_handler<GetBotInfoQuery>(std::move(promise))->send(bot_user_id, language_code, 0);
}

}  // namespace td
