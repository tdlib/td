//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/InputGroupCallId.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"

namespace td {

InputGroupCallId::InputGroupCallId(const tl_object_ptr<telegram_api::inputGroupCall> &input_group_call)
    : group_call_id(input_group_call->id_), access_hash(input_group_call->access_hash_) {
}

Result<InputGroupCallId> InputGroupCallId::from_group_call_id(const string &group_call_id) {
  if (group_call_id.empty()) {
    return InputGroupCallId();
  }

  auto splitted = split(group_call_id, '_');
  auto r_group_call_id = to_integer_safe<int64>(splitted.first);
  auto r_access_hash = to_integer_safe<int64>(splitted.second);
  if (r_group_call_id.is_error() || r_access_hash.is_error()) {
    return Status::Error("Invalid group call identifier specified");
  }

  InputGroupCallId result;
  result.group_call_id = r_group_call_id.ok();
  result.access_hash = r_access_hash.ok();
  return result;
}

string InputGroupCallId::get_group_call_id() const {
  if (is_valid()) {
    return PSTRING() << group_call_id << '_' << access_hash;
  }
  return string();
}

tl_object_ptr<telegram_api::inputGroupCall> InputGroupCallId::get_input_group_call() const {
  return make_tl_object<telegram_api::inputGroupCall>(group_call_id, access_hash);
}

StringBuilder &operator<<(StringBuilder &string_builder, InputGroupCallId input_group_call_id) {
  return string_builder << "input group call " << input_group_call_id.group_call_id;
}

}  // namespace td
