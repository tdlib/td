//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/InputGroupCallId.h"

#include "td/utils/logging.h"

namespace td {

InputGroupCallId::InputGroupCallId(const telegram_api::object_ptr<telegram_api::InputGroupCall> &input_group_call) {
  CHECK(input_group_call != nullptr);
  if (input_group_call->get_id() != telegram_api::inputGroupCall::ID) {
    LOG(ERROR) << "Receive " << to_string(input_group_call);
    return;
  }
  auto group_call = static_cast<const telegram_api::inputGroupCall *>(input_group_call.get());
  group_call_id = group_call->id_;
  access_hash = group_call->access_hash_;
}

telegram_api::object_ptr<telegram_api::inputGroupCall> InputGroupCallId::get_input_group_call() const {
  return telegram_api::make_object<telegram_api::inputGroupCall>(group_call_id, access_hash);
}

StringBuilder &operator<<(StringBuilder &string_builder, InputGroupCallId input_group_call_id) {
  return string_builder << "input group call " << input_group_call_id.group_call_id;
}

}  // namespace td
