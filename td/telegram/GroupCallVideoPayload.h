//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

struct GroupCallVideoPayloadFeedbackType {
  string type;
  string subtype;
};

struct GroupCallVideoPayloadParameter {
  string name;
  string value;
};

struct GroupCallVideoPayloadType {
  int32 id;
  string name;
  int32 clock_rate;
  int32 channel_count;
  vector<GroupCallVideoPayloadFeedbackType> feedback_types;
  vector<GroupCallVideoPayloadParameter> parameters;
};

struct GroupCallVideoExtension {
  int32 id;
  string name;
};

struct GroupCallVideoSourceGroup {
  vector<int32> sources;
  string semantics;
};

struct GroupCallVideoPayload {
  vector<GroupCallVideoPayloadType> payload_types;
  vector<GroupCallVideoExtension> extensions;
  vector<GroupCallVideoSourceGroup> source_groups;
};

bool operator==(const GroupCallVideoPayload &lhs, const GroupCallVideoPayload &rhs);

bool operator!=(const GroupCallVideoPayload &lhs, const GroupCallVideoPayload &rhs);

td_api::object_ptr<td_api::groupCallVideoPayload> get_group_call_video_payload_object(
    const GroupCallVideoPayload &payload);

Result<GroupCallVideoPayload> get_group_call_video_payload(string json, string &endpoint);

Result<string> encode_join_group_call_payload(td_api::object_ptr<td_api::groupCallPayload> &&payload,
                                              int32 audio_source,
                                              td_api::object_ptr<td_api::groupCallVideoPayload> &&video_payload);

Result<td_api::object_ptr<td_api::GroupCallJoinResponse>> get_group_call_join_response_object(string json);

}  // namespace td
