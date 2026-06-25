//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WebBrowserManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/WebBrowserSettings.hpp"

#include "td/utils/buffer.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/misc.h"
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

class UpdateWebBrowserSettingsQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> promise_;

 public:
  explicit UpdateWebBrowserSettingsQuery(
      Promise<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(bool open_external_browser, bool display_close_button) {
    send_query(G()->net_query_creator().create(
        telegram_api::account_updateWebBrowserSettings(0, open_external_browser, display_close_button)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_updateWebBrowserSettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ToggleWebBrowserSettingsExceptionQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ToggleWebBrowserSettingsExceptionQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(bool is_deletion, bool open_external_browser, const string &url) {
    int32 flags = 0;
    if (!is_deletion) {
      flags |= telegram_api::account_toggleWebBrowserSettingsException::OPEN_EXTERNAL_BROWSER_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::account_toggleWebBrowserSettingsException(flags, is_deletion, open_external_browser, url)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_toggleWebBrowserSettingsException>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ToggleWebBrowserSettingsExceptionQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class DeleteWebBrowserSettingsExceptionsQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> promise_;

 public:
  explicit DeleteWebBrowserSettingsExceptionsQuery(
      Promise<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_deleteWebBrowserSettingsExceptions()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_deleteWebBrowserSettingsExceptions>(packet);
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

void WebBrowserManager::start_up() {
  load_web_browser_settings();
}

void WebBrowserManager::tear_down() {
  parent_.reset();
}

void WebBrowserManager::on_authorization_success() {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  send_update_web_browser_settings();
  reload_web_browser_settings();
}

string WebBrowserManager::get_web_browser_settings_database_key() {
  return "web_browser_settings";
}

void WebBrowserManager::load_web_browser_settings() {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }

  auto log_event_string = G()->td_db()->get_binlog_pmc()->get(get_web_browser_settings_database_key());
  if (!log_event_string.empty()) {
    auto status = log_event_parse(settings_, log_event_string);
    if (status.is_error()) {
      LOG(ERROR) << "Failed to parse web browser settings from binlog: " << status;
      settings_ = WebBrowserSettings();
    }
  }
  send_update_web_browser_settings();

  if (settings_.get_hash() == 0) {
    reload_web_browser_settings();
  }
}

void WebBrowserManager::save_web_browser_settings() {
  G()->td_db()->get_binlog_pmc()->set(get_web_browser_settings_database_key(),
                                      log_event_store(settings_).as_slice().str());
}

void WebBrowserManager::reload_web_browser_settings() {
  auto request_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> result) {
        send_closure(actor_id, &WebBrowserManager::on_get_web_browser_settings, std::move(result), Promise<Unit>());
      });

  td_->create_handler<GetWebBrowserSettingsQuery>(std::move(request_promise))->send(settings_.get_hash());
}

void WebBrowserManager::on_get_web_browser_settings(
    Result<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> result, Promise<Unit> &&promise) {
  if (result.is_error()) {
    return promise.set_error(result.move_as_error());
  }

  auto web_browser_settings_ptr = result.move_as_ok();
  LOG(DEBUG) << "Receive " << to_string(web_browser_settings_ptr);
  if (web_browser_settings_ptr->get_id() == telegram_api::account_webBrowserSettingsNotModified::ID) {
    return promise.set_value(Unit());
  }
  auto new_settings = WebBrowserSettings(std::move(web_browser_settings_ptr));
  if (new_settings != settings_) {
    settings_ = std::move(new_settings);
    save_web_browser_settings();
    send_update_web_browser_settings();
  }
  promise.set_value(Unit());
}

void WebBrowserManager::on_update_web_browser_settings(
    telegram_api::object_ptr<telegram_api::updateWebBrowserSettings> &&update) {
  if (settings_.update_from(std::move(update))) {
    save_web_browser_settings();
    send_update_web_browser_settings();
  }
}

void WebBrowserManager::on_update_web_browser_exception(
    telegram_api::object_ptr<telegram_api::updateWebBrowserException> &&update) {
  if (settings_.update_from(std::move(update))) {
    save_web_browser_settings();
    send_update_web_browser_settings();
  }
}

void WebBrowserManager::update_web_browser_settings(bool open_external_browser, bool display_close_button,
                                                    Promise<Unit> &&promise) {
  auto request_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), promise = std::move(promise)](
          Result<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> result) mutable {
        send_closure(actor_id, &WebBrowserManager::on_get_web_browser_settings, std::move(result), std::move(promise));
      });

  td_->create_handler<UpdateWebBrowserSettingsQuery>(std::move(request_promise))
      ->send(open_external_browser, display_close_button);
}

void WebBrowserManager::add_web_browser_settings_exception(bool open_external_browser, const string &url,
                                                           Promise<Unit> &&promise) {
  td_->create_handler<ToggleWebBrowserSettingsExceptionQuery>(std::move(promise))
      ->send(false, open_external_browser, url);
}

void WebBrowserManager::remove_web_browser_settings_exception(const string &url, Promise<Unit> &&promise) {
  td_->create_handler<ToggleWebBrowserSettingsExceptionQuery>(std::move(promise))->send(true, false, url);
}

void WebBrowserManager::remove_all_web_browser_settings_exceptions(Promise<Unit> &&promise) {
  auto request_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), promise = std::move(promise)](
          Result<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> result) mutable {
        send_closure(actor_id, &WebBrowserManager::on_get_web_browser_settings, std::move(result), std::move(promise));
      });

  td_->create_handler<DeleteWebBrowserSettingsExceptionsQuery>(std::move(request_promise))->send();
}

void WebBrowserManager::get_web_browser_type(string &&url,
                                             Promise<td_api::object_ptr<td_api::WebBrowserType>> &&promise) {
  auto r_http_url = parse_url(url);
  if (r_http_url.is_error()) {
    to_lower_inplace(url);
    if (begins_with(url, "tonsite://")) {
      return promise.set_value(td_api::make_object<td_api::webBrowserTypeInApp>());
    }
    return promise.set_error(400, "Invalid HTTP URL specified");
  }
  bool open_external_browser = settings_.get_open_external_browser(r_http_url.ok().host_);
  if (open_external_browser) {
    promise.set_value(td_api::make_object<td_api::webBrowserTypeExternal>());
  } else {
    promise.set_value(td_api::make_object<td_api::webBrowserTypeInApp>());
  }
}

td_api::object_ptr<td_api::updateWebBrowserSettings> WebBrowserManager::get_update_web_browser_settings_object() const {
  return td_api::make_object<td_api::updateWebBrowserSettings>(settings_.get_web_browser_settings_object());
}

void WebBrowserManager::send_update_web_browser_settings() const {
  send_closure(G()->td(), &Td::send_update, get_update_web_browser_settings_object());
}

void WebBrowserManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }

  updates.push_back(get_update_web_browser_settings_object());
}

}  // namespace td
