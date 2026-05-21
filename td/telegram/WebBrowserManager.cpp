//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WebBrowserManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/Td.h"

#include "td/utils/buffer.h"
#include "td/utils/Promise.h"

namespace td {

class GetWebBrowserSettingsQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> promise_;

 public:
  explicit GetWebBrowserSettingsQuery(
      Promise<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::account_getWebBrowserSettings(hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getWebBrowserSettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

WebBrowserManager::WebBrowserManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void WebBrowserManager::tear_down() {
  parent_.reset();
}

void WebBrowserManager::reload_web_browser_settings() {
  auto request_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> result) {
        send_closure(actor_id, &WebBrowserManager::on_get_web_browser_settings, std::move(result));
      });

  td_->create_handler<GetWebBrowserSettingsQuery>(std::move(request_promise))->send(settings_.get_hash());
}

void WebBrowserManager::on_get_web_browser_settings(
    Result<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> result) {
  if (result.is_error()) {
    return;
  }

  auto web_browser_settings_ptr = result.move_as_ok();
  LOG(DEBUG) << "Receive " << to_string(web_browser_settings_ptr);
  if (web_browser_settings_ptr->get_id() == telegram_api::account_webBrowserSettingsNotModified::ID) {
    return;
  }
  auto new_settings = WebBrowserSettings(std::move(web_browser_settings_ptr));
  if (new_settings == settings_) {
    return;
  }
  settings_ = std::move(new_settings);

  send_update_web_browser_settings();
}

td_api::object_ptr<td_api::updateWebBrowserSettings> WebBrowserManager::get_update_web_browser_settings_object() const {
  return td_api::make_object<td_api::updateWebBrowserSettings>(settings_.get_web_browser_settings_object());
}

void WebBrowserManager::send_update_web_browser_settings() const {
  send_closure(G()->td(), &Td::send_update, get_update_web_browser_settings_object());
}

}  // namespace td
