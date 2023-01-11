//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class RequestedDialogType {
 public:
  RequestedDialogType() = default;

  explicit RequestedDialogType(td_api::object_ptr<td_api::keyboardButtonTypeRequestUser> &&request_user);

  explicit RequestedDialogType(td_api::object_ptr<td_api::keyboardButtonTypeRequestChat> &&request_dialog);

  explicit RequestedDialogType(telegram_api::object_ptr<telegram_api::RequestPeerType> &&peer_type, int32 button_id);

  td_api::object_ptr<td_api::KeyboardButtonType> get_keyboard_button_type_object() const;

  telegram_api::object_ptr<telegram_api::RequestPeerType> get_input_request_peer_type_object() const;

  int32 get_button_id() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

}  // namespace td
