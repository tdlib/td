//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

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

}  // namespace td
