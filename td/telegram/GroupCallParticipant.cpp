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

bool GroupCallParticipant::update_can_be_muted(bool can_manage, bool is_self, bool is_admin) {
  bool new_can_be_muted = false;
  bool new_can_be_unmuted = false;
  if (is_self) {
    // current user can be muted if !is_muted; after that is_muted && can_self_unmute
    // current user can be unmuted if is_muted && can_self_unmute; after that !is_muted
    new_can_be_muted = !is_muted;
    new_can_be_unmuted = is_muted && can_self_unmute;
  } else if (is_admin) {
    // admin user can be muted if can_manage && !is_muted; after that is_muted && can_self_unmute
    // admin user can't be unmuted
    new_can_be_muted = can_manage && !is_muted;
  } else {
    // other user can be muted if can_manage; after that is_muted && !can_self_unmute
    // other user can be unmuted if can_manage && is_muted && !can_self_unmute; after that is_muted && can_self_unmute
    new_can_be_muted = can_manage && (!is_muted || can_self_unmute);
    new_can_be_unmuted = can_manage && is_muted && !can_self_unmute;
  }
  if (new_can_be_muted != can_be_muted || new_can_be_unmuted != can_be_unmuted) {
    can_be_muted = new_can_be_muted;
    can_be_unmuted = new_can_be_unmuted;
    return true;
  }
  return false;
}

td_api::object_ptr<td_api::groupCallParticipant> GroupCallParticipant::get_group_call_participant_object(
    ContactsManager *contacts_manager) const {
  if (!is_valid()) {
    return nullptr;
  }

  return td_api::make_object<td_api::groupCallParticipant>(
      contacts_manager->get_user_id_object(user_id, "get_group_call_participant_object"), source, is_speaking,
      can_be_muted, can_be_unmuted, is_muted, can_self_unmute, order);
}

bool operator==(const GroupCallParticipant &lhs, const GroupCallParticipant &rhs) {
  return lhs.user_id == rhs.user_id && lhs.source == rhs.source && lhs.can_be_muted == rhs.can_be_muted &&
         lhs.can_be_unmuted == rhs.can_be_unmuted && lhs.is_muted == rhs.is_muted &&
         lhs.can_self_unmute == rhs.can_self_unmute && lhs.is_speaking == rhs.is_speaking && lhs.order == rhs.order;
}

bool operator!=(const GroupCallParticipant &lhs, const GroupCallParticipant &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const GroupCallParticipant &group_call_participant) {
  return string_builder << '[' << group_call_participant.user_id << " with source " << group_call_participant.source
                        << " and order " << group_call_participant.order << ']';
}

}  // namespace td
