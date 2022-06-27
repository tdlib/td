//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

void get_invite_text(Td *td, Promise<string> &&promise);

void save_app_log(Td *td, const string &type, DialogId dialog_id, tl_object_ptr<telegram_api::JSONValue> &&data,
                  Promise<Unit> &&promise);

}  // namespace td
