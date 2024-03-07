//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BusinessConnectionManager.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/Td.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/logging.h"

namespace td {

struct BusinessConnectionManager::BusinessConnection {
  string connection_id_;
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
    return !connection_id_.empty() && user_id_.is_valid() && !dc_id_.is_empty() && connection_date_ > 0;
  }

  td_api::object_ptr<td_api::businessConnection> get_business_connection_object(Td *td) const {
    return td_api::make_object<td_api::businessConnection>(
        connection_id_, td->contacts_manager_->get_user_id_object(user_id_, "businessConnection"), connection_date_,
        can_reply_, is_disabled_);
  }
};

BusinessConnectionManager::BusinessConnectionManager(Td *td, ActorShared<> parent)
    : td_(td), parent_(std::move(parent)) {
}

BusinessConnectionManager::~BusinessConnectionManager() = default;

void BusinessConnectionManager::tear_down() {
  parent_.reset();
}

void BusinessConnectionManager::on_update_bot_business_connect(
    telegram_api::object_ptr<telegram_api::botBusinessConnection> &&connection) {
  CHECK(connection != nullptr);
  auto business_connection = make_unique<BusinessConnection>(connection);
  if (!business_connection->is_valid()) {
    LOG(ERROR) << "Receive invalid " << to_string(connection);
    return;
  }

  auto &stored_connection = business_connections_[business_connection->connection_id_];
  stored_connection = std::move(business_connection);
  send_closure(
      G()->td(), &Td::send_update,
      td_api::make_object<td_api::updateBusinessConnection>(stored_connection->get_business_connection_object(td_)));
}

}  // namespace td
