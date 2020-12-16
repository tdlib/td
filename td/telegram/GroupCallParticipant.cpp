//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallParticipant.h"

#include "td/telegram/ContactsManager.h"

namespace td {

GroupCallParticipant::GroupCallParticipant(const tl_object_ptr<telegram_api::groupCallParticipant> &participant) {
  CHECK(participant != nullptr);
  user_id = UserId(participant->user_id_);
  source = participant->source_;
  is_muted = participant->muted_;
  can_self_unmute = participant->can_self_unmute_;
  if (!participant->left_) {
    joined_date = participant->date_;
    if ((participant->flags_ & telegram_api::groupCallParticipant::ACTIVE_DATE_MASK) != 0) {
      active_date = participant->active_date_;
    }
    if (joined_date < 0 || active_date < 0) {
      LOG(ERROR) << "Receive invalid " << to_string(participant);
      joined_date = 0;
      active_date = 0;
    }
  }
  is_just_joined = participant->just_joined_;
}

td_api::object_ptr<td_api::groupCallParticipant> GroupCallParticipant::get_group_call_participant_object(
    ContactsManager *contacts_manager) const {
  if (!is_valid()) {
    return nullptr;
  }

  return td_api::make_object<td_api::groupCallParticipant>(
      contacts_manager->get_user_id_object(user_id, "get_group_call_participant_object"), source, is_speaking, is_muted,
      can_self_unmute, order);
}

bool operator==(const GroupCallParticipant &lhs, const GroupCallParticipant &rhs) {
  return lhs.user_id == rhs.user_id && lhs.source == rhs.source && lhs.is_muted == rhs.is_muted &&
         lhs.can_self_unmute == rhs.can_self_unmute && lhs.joined_date == rhs.joined_date &&
         max(lhs.active_date, lhs.local_active_date) == max(rhs.active_date, rhs.local_active_date) &&
         lhs.is_speaking == rhs.is_speaking && lhs.order == rhs.order;
}

bool operator!=(const GroupCallParticipant &lhs, const GroupCallParticipant &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const GroupCallParticipant &group_call_participant) {
  return string_builder << '[' << group_call_participant.user_id << " with source " << group_call_participant.source
                        << " and order " << group_call_participant.order << ']';
}

}  // namespace td
