//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallVideoPayload.h"

#include "td/utils/algorithm.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/misc.h"

#include <algorithm>

namespace td {

bool operator==(const GroupCallVideoPayload &lhs, const GroupCallVideoPayload &rhs) {
  return lhs.sources == rhs.sources && lhs.endpoint == rhs.endpoint && lhs.json_payload == rhs.json_payload;
}

td_api::object_ptr<td_api::groupCallParticipantVideoInfo> get_group_call_participant_video_info_object(
    const GroupCallVideoPayload &payload) {
  if (payload.endpoint.empty() || payload.sources.empty()) {
    return nullptr;
  }
  return td_api::make_object<td_api::groupCallParticipantVideoInfo>(vector<int32>(payload.sources), payload.endpoint,
                                                                    payload.json_payload);
}

static vector<int32> get_group_call_video_sources(JsonValue &&value) {
  if (value.type() != JsonValue::Type::Object) {
    return {};
  }

  vector<int32> result;
  auto &value_object = value.get_object();
  auto r_sources = get_json_object_field(value_object, "sources", JsonValue::Type::Array, false);
  if (r_sources.is_error()) {
    return {};
  }
  auto sources = r_sources.move_as_ok();

  for (auto &source : sources.get_array()) {
    Slice source_str;
    if (source.type() == JsonValue::Type::String) {
      source_str = source.get_string();
    } else if (source.type() == JsonValue::Type::Number) {
      source_str = source.get_number();
    }
    auto r_source_id = to_integer_safe<int64>(source_str);
    if (r_source_id.is_ok()) {
      result.push_back(static_cast<int32>(r_source_id.ok()));
    }
  }

  return result;
}

GroupCallVideoPayload get_group_call_video_payload(string json) {
  string json_copy = json;
  auto r_value = json_decode(json_copy);
  if (r_value.is_error()) {
    return {};
  }

  auto value = r_value.move_as_ok();
  if (value.type() != JsonValue::Type::Object) {
    return {};
  }

  GroupCallVideoPayload result;
  result.json_payload = std::move(json);

  auto &value_object = value.get_object();
  auto r_endpoint = get_json_object_string_field(value_object, "endpoint", true);
  if (r_endpoint.is_ok()) {
    result.endpoint = r_endpoint.move_as_ok();
  }

  auto r_source_groups = get_json_object_field(value_object, "ssrc-groups", JsonValue::Type::Array, false);
  if (r_source_groups.is_ok()) {
    auto source_groups = r_source_groups.move_as_ok();
    for (auto &source_group_object : source_groups.get_array()) {
      append(result.sources, get_group_call_video_sources(std::move(source_group_object)));
    }
    td::unique(result.sources);
  }
  return result;
}

}  // namespace td
