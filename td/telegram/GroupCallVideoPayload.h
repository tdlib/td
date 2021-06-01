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

struct GroupCallVideoSourceGroup {
  string semantics;
  vector<int32> source_ids;
};

struct GroupCallVideoPayload {
  vector<GroupCallVideoSourceGroup> source_groups;
  string endpoint;
  bool is_paused = false;
};

bool operator==(const GroupCallVideoPayload &lhs, const GroupCallVideoPayload &rhs);

td_api::object_ptr<td_api::groupCallParticipantVideoInfo> get_group_call_participant_video_info_object(
    const GroupCallVideoPayload &payload);

GroupCallVideoPayload get_group_call_video_payload(const telegram_api::groupCallParticipantVideo *video);

}  // namespace td
