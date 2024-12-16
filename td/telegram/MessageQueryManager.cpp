//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageQueryManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

namespace td {

class ReportMessageDeliveryQuery final : public Td::ResultHandler {
  DialogId dialog_id_;

 public:
  void send(MessageFullId message_full_id, bool from_push) {
    dialog_id_ = message_full_id.get_dialog_id();
    auto input_peer = td_->dialog_manager_->get_input_peer(message_full_id.get_dialog_id(), AccessRights::Read);
    if (input_peer == nullptr) {
      return;
    }
    int32 flags = 0;
    if (from_push) {
      flags |= telegram_api::messages_reportMessagesDelivery::PUSH_MASK;
    }
    auto message_id = message_full_id.get_message_id();
    CHECK(message_id.is_valid());
    CHECK(message_id.is_server());
    send_query(G()->net_query_creator().create(telegram_api::messages_reportMessagesDelivery(
        flags, false /*ignored*/, std::move(input_peer), {message_id.get_server_message_id().get()})));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_reportMessagesDelivery>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    // ok
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "ReportMessageDeliveryQuery");
  }
};

MessageQueryManager::MessageQueryManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void MessageQueryManager::report_message_delivery(MessageFullId message_full_id, int32 until_date, bool from_push) {
  if (G()->unix_time() > until_date) {
    return;
  }
  td_->create_handler<ReportMessageDeliveryQuery>()->send(message_full_id, from_push);
}

void MessageQueryManager::tear_down() {
  parent_.reset();
}

}  // namespace td
