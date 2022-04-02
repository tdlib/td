//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogParticipant.h"
#include "td/telegram/td_api.h"

#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"

namespace td {

class Td;

void set_account_ttl(Td *td, int32 account_ttl, Promise<Unit> &&promise);

void get_account_ttl(Td *td, Promise<int32> &&promise);

void confirm_qr_code_authentication(Td *td, const string &link, Promise<td_api::object_ptr<td_api::session>> &&promise);

void get_active_sessions(Td *td, Promise<td_api::object_ptr<td_api::sessions>> &&promise);

void terminate_session(Td *td, int64 session_id, Promise<Unit> &&promise);

void terminate_all_other_sessions(Td *td, Promise<Unit> &&promise);

void toggle_session_can_accept_calls(Td *td, int64 session_id, bool can_accept_calls, Promise<Unit> &&promise);

void toggle_session_can_accept_secret_chats(Td *td, int64 session_id, bool can_accept_secret_chats,
                                            Promise<Unit> &&promise);

void set_inactive_session_ttl_days(Td *td, int32 authorization_ttl_days, Promise<Unit> &&promise);

void get_connected_websites(Td *td, Promise<td_api::object_ptr<td_api::connectedWebsites>> &&promise);

void disconnect_website(Td *td, int64 website_id, Promise<Unit> &&promise);

void disconnect_all_websites(Td *td, Promise<Unit> &&promise);

void set_default_group_administrator_rights(Td *td, AdministratorRights administrator_rights, Promise<Unit> &&promise);

void set_default_channel_administrator_rights(Td *td, AdministratorRights administrator_rights,
                                              Promise<Unit> &&promise);

}  // namespace td
