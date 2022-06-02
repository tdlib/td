//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Application.h"

#include "td/telegram/Global.h"
#include "td/telegram/Td.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

namespace td {

class GetInviteTextQuery final : public Td::ResultHandler {
  Promise<string> promise_;

 public:
  explicit GetInviteTextQuery(Promise<string> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::help_getInviteText()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getInviteText>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(std::move(result->message_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SaveAppLogQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SaveAppLogQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &type, DialogId dialog_id, tl_object_ptr<telegram_api::JSONValue> &&data) {
    CHECK(data != nullptr);
    vector<telegram_api::object_ptr<telegram_api::inputAppEvent>> input_app_events;
    input_app_events.push_back(
        make_tl_object<telegram_api::inputAppEvent>(G()->server_time_cached(), type, dialog_id.get(), std::move(data)));
    send_query(G()->net_query_creator().create_unauth(telegram_api::help_saveAppLog(std::move(input_app_events))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_saveAppLog>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    LOG_IF(ERROR, !result) << "Receive false from help.saveAppLog";
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

void get_invite_text(Td *td, Promise<string> &&promise) {
  td->create_handler<GetInviteTextQuery>(std::move(promise))->send();
}

void save_app_log(Td *td, const string &type, DialogId dialog_id, tl_object_ptr<telegram_api::JSONValue> &&data,
                  Promise<Unit> &&promise) {
  td->create_handler<SaveAppLogQuery>(std::move(promise))->send(type, dialog_id, std::move(data));
}

}  // namespace td
