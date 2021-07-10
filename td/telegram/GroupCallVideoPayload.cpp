//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallVideoPayload.h"

#include "td/utils/algorithm.h"

namespace td {

static bool operator==(const GroupCallVideoSourceGroup &lhs, const GroupCallVideoSourceGroup &rhs) {
  return lhs.semantics == rhs.semantics && lhs.source_ids == rhs.source_ids;
}

bool operator==(const GroupCallVideoPayload &lhs, const GroupCallVideoPayload &rhs) {
  return lhs.source_groups == rhs.source_groups && lhs.endpoint == rhs.endpoint && lhs.is_paused == rhs.is_paused;
}

bool GroupCallVideoPayload::is_empty() const {
  return endpoint.empty() || source_groups.empty();
}

td_api::object_ptr<td_api::groupCallParticipantVideoInfo>
GroupCallVideoPayload::get_group_call_participant_video_info_object() const {
  if (is_empty()) {
    return nullptr;
  }

  auto get_group_call_video_source_group_object = [](const GroupCallVideoSourceGroup &group) {
    return td_api::make_object<td_api::groupCallVideoSourceGroup>(group.semantics, vector<int32>(group.source_ids));
  };
  return td_api::make_object<td_api::groupCallParticipantVideoInfo>(
      transform(source_groups, get_group_call_video_source_group_object), endpoint, is_paused);
}

GroupCallVideoPayload::GroupCallVideoPayload(const telegram_api::groupCallParticipantVideo *video) {
  if (video == nullptr) {
    return;
  }

  endpoint = video->endpoint_;
  source_groups = transform(video->source_groups_, [](auto &&source_group) {
    GroupCallVideoSourceGroup result;
    result.semantics = source_group->semantics_;
    result.source_ids = source_group->sources_;
    return result;
  });
  is_paused = video->paused_;
}

}  // namespace td
