//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/RequestedDialogType.h"

namespace td {

RequestedDialogType::RequestedDialogType(td_api::object_ptr<td_api::keyboardButtonTypeRequestUser> &&request_user) {
}

RequestedDialogType::RequestedDialogType(td_api::object_ptr<td_api::keyboardButtonTypeRequestChat> &&request_dialog) {
}

RequestedDialogType::RequestedDialogType(telegram_api::object_ptr<telegram_api::RequestPeerType> &&chat_type, int32 button_id) {
}

td_api::object_ptr<td_api::KeyboardButtonType> RequestedDialogType::get_keyboard_button_type_object() const {
  return td_api::make_object<td_api::keyboardButtonTypeRequestUser>(0, false, false, false, false);
}

telegram_api::object_ptr<telegram_api::RequestPeerType> RequestedDialogType::get_input_request_peer_type_object()
    const {
  return telegram_api::make_object<telegram_api::requestPeerTypeUser>(0, false, false);
}

int32 RequestedDialogType::get_button_id() const {
  return 0;
}

}  // namespace td
