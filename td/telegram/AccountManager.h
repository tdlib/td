//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class AccountManager final : public Actor {
 public:
  AccountManager(Td *td, ActorShared<> parent);

  void set_default_message_ttl(int32 message_ttl, Promise<Unit> &&promise);

  void get_default_message_ttl(Promise<int32> &&promise);

  void set_account_ttl(int32 account_ttl, Promise<Unit> &&promise);

  void get_account_ttl(Promise<int32> &&promise);

  void confirm_qr_code_authentication(const string &link, Promise<td_api::object_ptr<td_api::session>> &&promise);

  void get_active_sessions(Promise<td_api::object_ptr<td_api::sessions>> &&promise);

  void terminate_session(int64 session_id, Promise<Unit> &&promise);

  void terminate_all_other_sessions(Promise<Unit> &&promise);

  void toggle_session_can_accept_calls(int64 session_id, bool can_accept_calls, Promise<Unit> &&promise);

  void toggle_session_can_accept_secret_chats(int64 session_id, bool can_accept_secret_chats, Promise<Unit> &&promise);

  void set_inactive_session_ttl_days(int32 authorization_ttl_days, Promise<Unit> &&promise);

  void get_connected_websites(Promise<td_api::object_ptr<td_api::connectedWebsites>> &&promise);

  void disconnect_website(int64 website_id, Promise<Unit> &&promise);

  void disconnect_all_websites(Promise<Unit> &&promise);

  void export_contact_token(Promise<td_api::object_ptr<td_api::userLink>> &&promise);

  void import_contact_token(const string &token, Promise<td_api::object_ptr<td_api::user>> &&promise);

  void invalidate_authentication_codes(vector<string> &&authentication_codes);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
