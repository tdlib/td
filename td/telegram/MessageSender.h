//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"

namespace td {

class Td;

td_api::object_ptr<td_api::MessageSender> get_message_sender_object_const(Td *td, UserId user_id, DialogId dialog_id,
                                                                          const char *source);

td_api::object_ptr<td_api::MessageSender> get_message_sender_object_const(Td *td, DialogId dialog_id,
                                                                          const char *source);

td_api::object_ptr<td_api::MessageSender> get_message_sender_object(Td *td, UserId user_id, DialogId dialog_id,
                                                                    const char *source);

td_api::object_ptr<td_api::MessageSender> get_message_sender_object(Td *td, DialogId dialog_id, const char *source);

}  // namespace td
