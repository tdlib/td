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
#include "td/telegram/UpdatesManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

#include <algorithm>

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

class CanBotSendMessageQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit CanBotSendMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(UserId bot_user_id) {
    auto r_input_user = td_->contacts_manager_->get_input_user(bot_user_id);
    if (r_input_user.is_error()) {
      return on_error(r_input_user.move_as_error());
    }
    send_query(
        G()->net_query_creator().create(telegram_api::bots_canSendMessage(r_input_user.move_as_ok()), {{bot_user_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_canSendMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    if (result_ptr.ok()) {
      promise_.set_value(Unit());
    } else {
      promise_.set_error(Status::Error(404, "Not Found"));
    }
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class AllowBotSendMessageQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit AllowBotSendMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(UserId bot_user_id) {
    auto r_input_user = td_->contacts_manager_->get_input_user(bot_user_id);
    if (r_input_user.is_error()) {
      return on_error(r_input_user.move_as_error());
    }
    send_query(G()->net_query_creator().create(telegram_api::bots_allowSendMessage(r_input_user.move_as_ok()),
                                               {{bot_user_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::bots_allowSendMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for AllowBotSendMessageQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
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
  bool set_info_ = false;

  void invalidate_bot_info() {
    if (set_info_) {
      td_->contacts_manager_->invalidate_user_full(bot_user_id_);
    }
  }

 public:
  explicit SetBotInfoQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(UserId bot_user_id, const string &language_code, bool set_name, const string &name, bool set_description,
            const string &description, bool set_about, const string &about) {
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
    set_info_ = set_about || set_description;
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
    if (set_info_) {
      invalidate_bot_info();
      if (!td_->auth_manager_->is_bot()) {
        return td_->contacts_manager_->reload_user_full(bot_user_id_, std::move(promise_), "SetBotInfoQuery");
      }
    }
    if (set_name_) {
      return td_->contacts_manager_->reload_user(bot_user_id_, std::move(promise_), "SetBotInfoQuery");
    }
    // invalidation is enough for bots if name wasn't changed
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    invalidate_bot_info();
    promise_.set_error(std::move(status));
  }
};

class GetBotInfoQuery final : public Td::ResultHandler {
  vector<Promise<string>> name_promises_;
  vector<Promise<string>> description_promises_;
  vector<Promise<string>> about_promises_;

 public:
  GetBotInfoQuery(vector<Promise<string>> name_promises, vector<Promise<string>> description_promises,
                  vector<Promise<string>> about_promises)
      : name_promises_(std::move(name_promises))
      , description_promises_(std::move(description_promises))
      , about_promises_(std::move(about_promises)) {
  }

  void send(UserId bot_user_id, const string &language_code) {
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
    LOG(INFO) << "Receive result for GetBotInfoQuery: " << to_string(result);
    for (auto &promise : name_promises_) {
      promise.set_value(string(result->name_));
    }
    for (auto &promise : description_promises_) {
      promise.set_value(string(result->description_));
    }
    for (auto &promise : about_promises_) {
      promise.set_value(string(result->about_));
    }
  }

  void on_error(Status status) final {
    fail_promises(name_promises_, status.clone());
    fail_promises(description_promises_, status.clone());
    fail_promises(about_promises_, status.clone());
  }
};

BotInfoManager::BotInfoManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void BotInfoManager::tear_down() {
  parent_.reset();
}

void BotInfoManager::hangup() {
  auto set_queries = std::move(pending_set_bot_info_queries_);
  auto get_queries = std::move(pending_get_bot_info_queries_);

  for (auto &query : set_queries) {
    query.promise_.set_error(Global::request_aborted_error());
  }
  for (auto &query : get_queries) {
    query.promise_.set_error(Global::request_aborted_error());
  }

  stop();
}

void BotInfoManager::timeout_expired() {
  auto set_queries = std::move(pending_set_bot_info_queries_);
  reset_to_empty(pending_set_bot_info_queries_);
  auto get_queries = std::move(pending_get_bot_info_queries_);
  reset_to_empty(pending_get_bot_info_queries_);

  std::stable_sort(set_queries.begin(), set_queries.end(),
                   [](const PendingSetBotInfoQuery &lhs, const PendingSetBotInfoQuery &rhs) {
                     return lhs.bot_user_id_.get() < rhs.bot_user_id_.get() ||
                            (lhs.bot_user_id_ == rhs.bot_user_id_ && lhs.language_code_ < rhs.language_code_);
                   });
  for (size_t i = 0; i < set_queries.size();) {
    bool has_value[3] = {false, false, false};
    string values[3];
    vector<Promise<Unit>> promises;
    size_t j = i;
    while (j < set_queries.size() && set_queries[i].bot_user_id_ == set_queries[j].bot_user_id_ &&
           set_queries[i].language_code_ == set_queries[j].language_code_) {
      has_value[set_queries[j].type_] = true;
      values[set_queries[j].type_] = std::move(set_queries[j].value_);
      promises.push_back(std::move(set_queries[j].promise_));
      j++;
    }
    auto promise = PromiseCreator::lambda([promises = std::move(promises)](Result<Unit> &&result) mutable {
      if (result.is_error()) {
        fail_promises(promises, result.move_as_error());
      } else {
        set_promises(promises);
      }
    });
    td_->create_handler<SetBotInfoQuery>(std::move(promise))
        ->send(set_queries[i].bot_user_id_, set_queries[i].language_code_, has_value[0], values[0], has_value[1],
               values[1], has_value[2], values[2]);
    i = j;
  }

  std::stable_sort(get_queries.begin(), get_queries.end(),
                   [](const PendingGetBotInfoQuery &lhs, const PendingGetBotInfoQuery &rhs) {
                     return lhs.bot_user_id_.get() < rhs.bot_user_id_.get() ||
                            (lhs.bot_user_id_ == rhs.bot_user_id_ && lhs.language_code_ < rhs.language_code_);
                   });
  for (size_t i = 0; i < get_queries.size();) {
    vector<Promise<string>> promises[3];
    size_t j = i;
    while (j < get_queries.size() && get_queries[i].bot_user_id_ == get_queries[j].bot_user_id_ &&
           get_queries[i].language_code_ == get_queries[j].language_code_) {
      promises[get_queries[j].type_].push_back(std::move(get_queries[j].promise_));
      j++;
    }
    td_->create_handler<GetBotInfoQuery>(std::move(promises[0]), std::move(promises[1]), std::move(promises[2]))
        ->send(get_queries[i].bot_user_id_, get_queries[i].language_code_);
    i = j;
  }
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

void BotInfoManager::can_bot_send_messages(UserId bot_user_id, Promise<Unit> &&promise) {
  td_->create_handler<CanBotSendMessageQuery>(std::move(promise))->send(bot_user_id);
}

void BotInfoManager::allow_bot_to_send_messages(UserId bot_user_id, Promise<Unit> &&promise) {
  td_->create_handler<AllowBotSendMessageQuery>(std::move(promise))->send(bot_user_id);
}

void BotInfoManager::add_pending_set_query(UserId bot_user_id, const string &language_code, int type,
                                           const string &value, Promise<Unit> &&promise) {
  pending_set_bot_info_queries_.emplace_back(bot_user_id, language_code, type, value, std::move(promise));
  if (!has_timeout()) {
    set_timeout_in(MAX_QUERY_DELAY);
  }
}

void BotInfoManager::add_pending_get_query(UserId bot_user_id, const string &language_code, int type,
                                           Promise<string> &&promise) {
  pending_get_bot_info_queries_.emplace_back(bot_user_id, language_code, type, std::move(promise));
  if (!has_timeout()) {
    set_timeout_in(MAX_QUERY_DELAY);
  }
}

void BotInfoManager::set_bot_name(UserId bot_user_id, const string &language_code, const string &name,
                                  Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, validate_bot_language_code(language_code));
  add_pending_set_query(bot_user_id, language_code, 0, name, std::move(promise));
}

void BotInfoManager::get_bot_name(UserId bot_user_id, const string &language_code, Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, validate_bot_language_code(language_code));
  add_pending_get_query(bot_user_id, language_code, 0, std::move(promise));
}

void BotInfoManager::set_bot_info_description(UserId bot_user_id, const string &language_code,
                                              const string &description, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, validate_bot_language_code(language_code));
  add_pending_set_query(bot_user_id, language_code, 1, description, std::move(promise));
}

void BotInfoManager::get_bot_info_description(UserId bot_user_id, const string &language_code,
                                              Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, validate_bot_language_code(language_code));
  add_pending_get_query(bot_user_id, language_code, 1, std::move(promise));
}

void BotInfoManager::set_bot_info_about(UserId bot_user_id, const string &language_code, const string &about,
                                        Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, validate_bot_language_code(language_code));
  add_pending_set_query(bot_user_id, language_code, 2, about, std::move(promise));
}

void BotInfoManager::get_bot_info_about(UserId bot_user_id, const string &language_code, Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, validate_bot_language_code(language_code));
  add_pending_get_query(bot_user_id, language_code, 2, std::move(promise));
}

}  // namespace td
