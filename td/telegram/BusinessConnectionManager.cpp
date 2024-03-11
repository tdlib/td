//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BusinessConnectionManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"

namespace td {

class GetBotBusinessConnectionQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::Updates>> promise_;

 public:
  explicit GetBotBusinessConnectionQuery(Promise<telegram_api::object_ptr<telegram_api::Updates>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const BusinessConnectionId &connection_id) {
    send_query(G()->net_query_creator().create(telegram_api::account_getBotBusinessConnection(connection_id.get())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getBotBusinessConnection>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetBotBusinessConnectionQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

struct BusinessConnectionManager::BusinessConnection {
  BusinessConnectionId connection_id_;
  UserId user_id_;
  DcId dc_id_;
  int32 connection_date_ = 0;
  bool can_reply_ = false;
  bool is_disabled_ = false;

  explicit BusinessConnection(const telegram_api::object_ptr<telegram_api::botBusinessConnection> &connection)
      : connection_id_(connection->connection_id_)
      , user_id_(connection->user_id_)
      , dc_id_(DcId::create(connection->dc_id_))
      , connection_date_(connection->date_)
      , can_reply_(connection->can_reply_)
      , is_disabled_(connection->disabled_) {
  }

  BusinessConnection(const BusinessConnection &) = delete;
  BusinessConnection &operator=(const BusinessConnection &) = delete;
  BusinessConnection(BusinessConnection &&) = delete;
  BusinessConnection &operator=(BusinessConnection &&) = delete;
  ~BusinessConnection() = default;

  bool is_valid() const {
    return connection_id_.is_valid() && user_id_.is_valid() && !dc_id_.is_empty() && connection_date_ > 0;
  }

  td_api::object_ptr<td_api::businessConnection> get_business_connection_object(Td *td) const {
    return td_api::make_object<td_api::businessConnection>(
        connection_id_.get(), td->contacts_manager_->get_user_id_object(user_id_, "businessConnection"),
        connection_date_, can_reply_, is_disabled_);
  }
};

BusinessConnectionManager::BusinessConnectionManager(Td *td, ActorShared<> parent)
    : td_(td), parent_(std::move(parent)) {
}

BusinessConnectionManager::~BusinessConnectionManager() = default;

void BusinessConnectionManager::tear_down() {
  parent_.reset();
}

Status BusinessConnectionManager::check_business_connection(const BusinessConnectionId &connection_id,
                                                            DialogId dialog_id) const {
  auto connection = business_connections_.get_pointer(connection_id);
  if (connection == nullptr) {
    return Status::Error(400, "Business connection not found");
  }
  if (dialog_id.get_type() != DialogType::User) {
    return Status::Error(400, "Chat must be a private chat");
  }
  if (dialog_id == DialogId(connection->user_id_)) {
    return Status::Error(400, "Messages must not be sent to self");
  }
  // no need to check connection->can_reply_ and connection->is_disabled_
  return Status::OK();
}

DcId BusinessConnectionManager::get_business_connection_dc_id(const BusinessConnectionId &connection_id) const {
  if (connection_id.is_empty()) {
    return DcId::main();
  }
  auto connection = business_connections_.get_pointer(connection_id);
  CHECK(connection != nullptr);
  return connection->dc_id_;
}

void BusinessConnectionManager::on_update_bot_business_connect(
    telegram_api::object_ptr<telegram_api::botBusinessConnection> &&connection) {
  CHECK(connection != nullptr);
  auto business_connection = make_unique<BusinessConnection>(connection);
  if (!business_connection->is_valid()) {
    LOG(ERROR) << "Receive invalid " << to_string(connection);
    return;
  }
  if (!td_->auth_manager_->is_bot()) {
    LOG(ERROR) << "Receive " << to_string(connection);
    return;
  }

  auto &stored_connection = business_connections_[business_connection->connection_id_];
  stored_connection = std::move(business_connection);
  send_closure(
      G()->td(), &Td::send_update,
      td_api::make_object<td_api::updateBusinessConnection>(stored_connection->get_business_connection_object(td_)));
}

void BusinessConnectionManager::on_update_bot_new_business_message(
    const BusinessConnectionId &connection_id, telegram_api::object_ptr<telegram_api::Message> &&message) {
  if (!td_->auth_manager_->is_bot() || !connection_id.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(message);
    return;
  }
  auto message_object = td_->messages_manager_->get_business_message_object(std::move(message));
  if (message_object == nullptr) {
    return;
  }
  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateNewBusinessMessage>(connection_id.get(), std::move(message_object)));
}

void BusinessConnectionManager::on_update_bot_edit_business_message(
    const BusinessConnectionId &connection_id, telegram_api::object_ptr<telegram_api::Message> &&message) {
  if (!td_->auth_manager_->is_bot() || !connection_id.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(message);
    return;
  }
  auto message_object = td_->messages_manager_->get_business_message_object(std::move(message));
  if (message_object == nullptr) {
    return;
  }
  send_closure(
      G()->td(), &Td::send_update,
      td_api::make_object<td_api::updateBusinessMessageEdited>(connection_id.get(), std::move(message_object)));
}

void BusinessConnectionManager::on_update_bot_delete_business_messages(const BusinessConnectionId &connection_id,
                                                                       DialogId dialog_id, vector<int32> &&messages) {
  if (!td_->auth_manager_->is_bot() || !connection_id.is_valid() || dialog_id.get_type() != DialogType::User) {
    LOG(ERROR) << "Receive deletion of messages " << messages << " in " << dialog_id;
    return;
  }
  vector<int64> message_ids;
  for (auto message : messages) {
    message_ids.push_back(MessageId(ServerMessageId(message)).get());
  }
  td_->dialog_manager_->force_create_dialog(dialog_id, "on_update_bot_delete_business_messages", true);
  send_closure(
      G()->td(), &Td::send_update,
      td_api::make_object<td_api::updateBusinessMessagesDeleted>(
          connection_id.get(), td_->dialog_manager_->get_chat_id_object(dialog_id, "updateBusinessMessageDeleted"),
          std::move(message_ids)));
}

void BusinessConnectionManager::get_business_connection(
    const BusinessConnectionId &connection_id, Promise<td_api::object_ptr<td_api::businessConnection>> &&promise) {
  auto connection = business_connections_.get_pointer(connection_id);
  if (connection != nullptr) {
    return promise.set_value(connection->get_business_connection_object(td_));
  }

  if (connection_id.is_empty()) {
    return promise.set_error(Status::Error(400, "Connection iedntifier must be non-empty"));
  }

  auto &queries = get_business_connection_queries_[connection_id];
  queries.push_back(std::move(promise));
  if (queries.size() == 1u) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this), connection_id](Result<telegram_api::object_ptr<telegram_api::Updates>> r_updates) {
          send_closure(actor_id, &BusinessConnectionManager::on_get_business_connection, connection_id,
                       std::move(r_updates));
        });
    td_->create_handler<GetBotBusinessConnectionQuery>(std::move(query_promise))->send(connection_id);
  }
}

void BusinessConnectionManager::on_get_business_connection(
    const BusinessConnectionId &connection_id, Result<telegram_api::object_ptr<telegram_api::Updates>> r_updates) {
  G()->ignore_result_if_closing(r_updates);
  auto queries_it = get_business_connection_queries_.find(connection_id);
  CHECK(queries_it != get_business_connection_queries_.end());
  CHECK(!queries_it->second.empty());
  auto promises = std::move(queries_it->second);
  get_business_connection_queries_.erase(queries_it);
  if (r_updates.is_error()) {
    return fail_promises(promises, r_updates.move_as_error());
  }
  auto connection = business_connections_.get_pointer(connection_id);
  if (connection != nullptr) {
    for (auto &promise : promises) {
      promise.set_value(connection->get_business_connection_object(td_));
    }
    return;
  }

  auto updates_ptr = r_updates.move_as_ok();
  if (updates_ptr->get_id() != telegram_api::updates::ID) {
    LOG(ERROR) << "Receive " << to_string(updates_ptr);
    return fail_promises(promises, Status::Error(500, "Receive invalid business connection info"));
  }
  auto updates = telegram_api::move_object_as<telegram_api::updates>(updates_ptr);
  if (updates->updates_.size() != 1 || updates->updates_[0]->get_id() != telegram_api::updateBotBusinessConnect::ID) {
    if (updates->updates_.empty()) {
      return fail_promises(promises, Status::Error(400, "Business connnection not found"));
    }
    LOG(ERROR) << "Receive " << to_string(updates);
    return fail_promises(promises, Status::Error(500, "Receive invalid business connection info"));
  }
  auto update = telegram_api::move_object_as<telegram_api::updateBotBusinessConnect>(updates->updates_[0]);

  td_->contacts_manager_->on_get_users(std::move(updates->users_), "on_get_business_connection");
  td_->contacts_manager_->on_get_chats(std::move(updates->chats_), "on_get_business_connection");

  auto business_connection = make_unique<BusinessConnection>(update->connection_);
  if (!business_connection->is_valid() || connection_id != business_connection->connection_id_) {
    LOG(ERROR) << "Receive for " << connection_id << ": " << to_string(update->connection_);
    return fail_promises(promises, Status::Error(500, "Receive invalid business connection info"));
  }

  auto &stored_connection = business_connections_[connection_id];
  CHECK(stored_connection == nullptr);
  stored_connection = std::move(business_connection);
  for (auto &promise : promises) {
    promise.set_value(stored_connection->get_business_connection_object(td_));
  }
}

}  // namespace td
