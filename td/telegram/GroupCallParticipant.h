//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

struct GroupCallParticipant {
  UserId user_id;
  int32 source = 0;
  bool is_muted = false;
  bool can_self_unmute = false;
  int32 joined_date = 0;
  int32 active_date = 0;

  bool is_speaking = false;
  int64 order = 0;

  bool is_valid() const {
    return user_id.is_valid();
  }

  GroupCallParticipant() = default;

  GroupCallParticipant(tl_object_ptr<telegram_api::groupCallParticipant> &&participant);

  td_api::object_ptr<td_api::groupCallParticipant> get_group_call_participant_object(Td *td) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const GroupCallParticipant &group_call_participant);

}  // namespace td
