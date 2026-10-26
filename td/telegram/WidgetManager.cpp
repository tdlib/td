//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WidgetManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

namespace td {

class GetBotWidgetQueryAnswerQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::widgetQueryAnswer>> promise_;
  DialogId dialog_id_;
  MessageId message_id_;

 public:
  explicit GetBotWidgetQueryAnswerQuery(Promise<td_api::object_ptr<td_api::widgetQueryAnswer>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, MessageId message_id, const string &data) {
    dialog_id_ = dialog_id;
    message_id_ = message_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);

    auto query_type = telegram_api::make_object<telegram_api::widgetQueryTypeData>(0, false);
    auto net_query = G()->net_query_creator().create(telegram_api::messages_getBotWidgetQueryAnswer(
        std::move(input_peer), message_id.get_server_message_id().get(), std::move(query_type), BufferSlice(data)));
    net_query->need_resend_on_503_ = false;
    send_query(std::move(net_query));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getBotWidgetQueryAnswer>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto answer = result_ptr.move_as_ok();
    promise_.set_value(td_api::make_object<td_api::widgetQueryAnswer>(std::move(answer->result_)));
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_message_error(dialog_id_, message_id_, status, "GetBotWidgetQueryAnswerQuery");
    if (status.message() == "BOT_RESPONSE_TIMEOUT") {
      status = Status::Error(502, "The bot is not responding");
    }
    promise_.set_error(std::move(status));
  }
};

WidgetManager::WidgetManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

WidgetManager::~WidgetManager() = default;

void WidgetManager::tear_down() {
  parent_.reset();
}

void WidgetManager::send_widget_query(MessageFullId message_full_id, string &&data,
                                      Promise<td_api::object_ptr<td_api::widgetQueryAnswer>> &&promise) {
  auto dialog_id = message_full_id.get_dialog_id();
  TRY_STATUS_PROMISE(
      promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read, "send_widget_query"));

  if (!td_->messages_manager_->have_message_force(message_full_id, "send_widget_query")) {
    return promise.set_error(400, "Message not found");
  }
  auto message_id = message_full_id.get_message_id();
  if (!message_id.is_server()) {
    return promise.set_error(400, "Wrong message identifier");
  }

  td_->create_handler<GetBotWidgetQueryAnswerQuery>(std::move(promise))->send(dialog_id, message_id, data);
}

}  // namespace td
