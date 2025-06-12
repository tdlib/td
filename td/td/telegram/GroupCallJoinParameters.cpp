//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallJoinParameters.h"

#include "td/telegram/misc.h"

namespace td {

Result<GroupCallJoinParameters> GroupCallJoinParameters::get_group_call_join_parameters(
    td_api::object_ptr<td_api::groupCallJoinParameters> &&parameters, bool allow_empty) {
  GroupCallJoinParameters result;
  if (parameters == nullptr) {
    if (!allow_empty) {
      return Status::Error(400, "Join parameters must be non-empty");
    }
  } else {
    if (!clean_input_string(parameters->payload_)) {
      return Status::Error(400, "Strings must be encoded in UTF-8");
    }
    if (parameters->payload_.empty() || parameters->audio_source_id_ == 0) {
      if (!allow_empty) {
        return Status::Error(400, "Join parameters must be non-empty");
      }
    } else {
      result.payload_ = std::move(parameters->payload_);
      result.audio_source_ = parameters->audio_source_id_;
      result.is_muted_ = parameters->is_muted_;
      result.is_my_video_enabled_ = parameters->is_my_video_enabled_;
    }
  }
  return std::move(result);
}

}  // namespace td
