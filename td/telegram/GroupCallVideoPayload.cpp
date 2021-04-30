//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallVideoPayload.h"

#include "td/utils/JsonBuilder.h"

namespace td {

void get_group_call_video_payload(string json, string &endpoint) {
  auto r_value = json_decode(json);
  if (r_value.is_error()) {
    return;
  }

  auto value = r_value.move_as_ok();
  if (value.type() != JsonValue::Type::Object) {
    return;
  }

  auto &value_object = value.get_object();
  auto r_endpoint = get_json_object_string_field(value_object, "endpoint", true);
  if (r_endpoint.is_ok()) {
    endpoint = r_endpoint.move_as_ok();
  }
}

}  // namespace td
