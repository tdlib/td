//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/WebBrowserSettings.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class WebBrowserManager final : public Actor {
 public:
  WebBrowserManager(Td *td, ActorShared<> parent);

  void on_authorization_success();

  void reload_web_browser_settings();

  void on_update_web_browser_settings(telegram_api::object_ptr<telegram_api::updateWebBrowserSettings> &&update);

  void on_update_web_browser_exception(telegram_api::object_ptr<telegram_api::updateWebBrowserException> &&update);

  void update_web_browser_settings(bool open_external_browser, bool display_close_button, Promise<Unit> &&promise);

  void add_web_browser_settings_exception(bool open_external_browser, const string &url, Promise<Unit> &&promise);

  void remove_web_browser_settings_exception(const string &url, Promise<Unit> &&promise);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  void start_up() final;

  void tear_down() final;

  void on_get_web_browser_settings(Result<telegram_api::object_ptr<telegram_api::account_WebBrowserSettings>> result,
                                   Promise<Unit> &&promise);

  td_api::object_ptr<td_api::updateWebBrowserSettings> get_update_web_browser_settings_object() const;

  void send_update_web_browser_settings() const;

  string get_web_browser_settings_database_key();

  void load_web_browser_settings();

  void save_web_browser_settings();

  Td *td_;
  ActorShared<> parent_;

  WebBrowserSettings settings_;
};

}  // namespace td
