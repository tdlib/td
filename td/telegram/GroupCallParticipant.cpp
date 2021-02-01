//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallParticipant.h"

#include "td/telegram/ContactsManager.h"

#include "td/utils/logging.h"

namespace td {

GroupCallParticipant::GroupCallParticipant(const tl_object_ptr<telegram_api::groupCallParticipant> &participant) {
  CHECK(participant != nullptr);
  user_id = UserId(participant->user_id_);
  audio_source = participant->source_;
  is_muted = participant->muted_;
  can_self_unmute = participant->can_self_unmute_;
  is_muted_only_for_self = participant->muted_by_you_;
  if ((participant->flags_ & telegram_api::groupCallParticipant::VOLUME_MASK) != 0) {
    volume_level = participant->volume_;
    if (volume_level < MIN_VOLUME_LEVEL || volume_level > MAX_VOLUME_LEVEL) {
      LOG(ERROR) << "Receive " << to_string(participant);
      volume_level = 10000;
    }
    is_volume_level_local = (participant->flags_ & telegram_api::groupCallParticipant::VOLUME_BY_ADMIN_MASK) == 0;
  }
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
  is_min = (participant->flags_ & telegram_api::groupCallParticipant::MIN_MASK) != 0;
}

bool GroupCallParticipant::is_versioned_update(const tl_object_ptr<telegram_api::groupCallParticipant> &participant) {
  return participant->just_joined_ || participant->left_ || participant->versioned_;
}

int32 GroupCallParticipant::get_volume_level() const {
  return pending_volume_level != 0 ? pending_volume_level : volume_level;
}

bool GroupCallParticipant::update_can_be_muted(bool can_manage, bool is_self, bool is_admin) {
  bool new_can_be_muted_for_all_users = false;
  bool new_can_be_unmuted_for_all_users = false;
  bool new_can_be_muted_only_for_self = !can_manage && !is_muted_only_for_self;
  bool new_can_be_unmuted_only_for_self = !can_manage && is_muted_only_for_self;
  if (is_self) {
    // current user can be muted if !is_muted; after that is_muted && can_self_unmute
    // current user can be unmuted if is_muted && can_self_unmute; after that !is_muted
    new_can_be_muted_for_all_users = !is_muted;
    new_can_be_unmuted_for_all_users = is_muted && can_self_unmute;
    new_can_be_muted_only_for_self = false;
    new_can_be_unmuted_only_for_self = false;
  } else if (is_admin) {
    // admin user can be muted if can_manage && !is_muted; after that is_muted && can_self_unmute
    // admin user can't be unmuted
    new_can_be_muted_for_all_users = can_manage && !is_muted;
  } else {
    // other users can be muted if can_manage; after that is_muted && !can_self_unmute
    // other users can be unmuted if can_manage && is_muted && !can_self_unmute; after that is_muted && can_self_unmute
    new_can_be_muted_for_all_users = can_manage && (!is_muted || can_self_unmute);
    new_can_be_unmuted_for_all_users = can_manage && is_muted && !can_self_unmute;
  }
  if (new_can_be_muted_for_all_users != can_be_muted_for_all_users ||
      new_can_be_unmuted_for_all_users != can_be_unmuted_for_all_users ||
      new_can_be_muted_only_for_self != can_be_muted_only_for_self ||
      new_can_be_unmuted_only_for_self != can_be_unmuted_only_for_self) {
    can_be_muted_for_all_users = new_can_be_muted_for_all_users;
    can_be_unmuted_for_all_users = new_can_be_unmuted_for_all_users;
    can_be_muted_only_for_self = new_can_be_muted_only_for_self;
    can_be_unmuted_only_for_self = new_can_be_unmuted_only_for_self;
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
      contacts_manager->get_user_id_object(user_id, "get_group_call_participant_object"), audio_source, is_speaking,
      can_be_muted_for_all_users, can_be_unmuted_for_all_users, can_be_muted_only_for_self,
      can_be_unmuted_only_for_self, is_muted, can_self_unmute, get_volume_level(), order);
}

bool operator==(const GroupCallParticipant &lhs, const GroupCallParticipant &rhs) {
  return lhs.user_id == rhs.user_id && lhs.audio_source == rhs.audio_source &&
         lhs.can_be_muted_for_all_users == rhs.can_be_muted_for_all_users &&
         lhs.can_be_unmuted_for_all_users == rhs.can_be_unmuted_for_all_users &&
         lhs.can_be_muted_only_for_self == rhs.can_be_muted_only_for_self &&
         lhs.can_be_unmuted_only_for_self == rhs.can_be_unmuted_only_for_self && lhs.is_muted == rhs.is_muted &&
         lhs.can_self_unmute == rhs.can_self_unmute && lhs.is_speaking == rhs.is_speaking &&
         lhs.get_volume_level() == rhs.get_volume_level() && lhs.order == rhs.order;
}

bool operator!=(const GroupCallParticipant &lhs, const GroupCallParticipant &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const GroupCallParticipant &group_call_participant) {
  return string_builder << '[' << group_call_participant.user_id << " with source "
                        << group_call_participant.audio_source << " and order " << group_call_participant.order << ']';
}

}  // namespace td
