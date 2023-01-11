//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/RequestedDialogType.h"

namespace td {

RequestedDialogType::RequestedDialogType(td_api::object_ptr<td_api::RequestedChatType> &&chat_type) {
}

RequestedDialogType::RequestedDialogType(telegram_api::object_ptr<telegram_api::RequestPeerType> &&chat_type) {
}

td_api::object_ptr<td_api::RequestedChatType> RequestedDialogType::get_requested_chat_type_object() const {
  return td_api::make_object<td_api::requestedChatTypePrivate>();
}

telegram_api::object_ptr<telegram_api::RequestPeerType> RequestedDialogType::get_input_request_peer_type_object()
    const {
  return telegram_api::make_object<telegram_api::requestPeerTypeUser>(0, false, false);
}

}  // namespace td
