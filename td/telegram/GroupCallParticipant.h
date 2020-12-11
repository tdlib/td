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

class ContactsManager;

struct GroupCallParticipant {
  UserId user_id;
  int32 source = 0;
  bool is_muted = false;
  bool can_self_unmute = false;
  int32 joined_date = 0;
  int32 active_date = 0;

  bool is_just_joined = false;
  bool is_speaking = false;
  int32 local_active_date = 0;
  int64 order = 0;

  int64 get_real_order() const {
    return (static_cast<int64>(max(active_date, local_active_date)) << 32) + joined_date;
  }

  bool is_valid() const {
    return user_id.is_valid();
  }

  GroupCallParticipant() = default;

  explicit GroupCallParticipant(const tl_object_ptr<telegram_api::groupCallParticipant> &participant);

  td_api::object_ptr<td_api::groupCallParticipant> get_group_call_participant_object(
      ContactsManager *contacts_manager) const;
};

bool operator==(const GroupCallParticipant &lhs, const GroupCallParticipant &rhs);

bool operator!=(const GroupCallParticipant &lhs, const GroupCallParticipant &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const GroupCallParticipant &group_call_participant);

}  // namespace td
