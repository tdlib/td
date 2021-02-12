//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
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
  int32 audio_source = 0;
  int32 joined_date = 0;
  int32 active_date = 0;
  int32 volume_level = 10000;
  bool is_volume_level_local = false;
  bool server_is_muted_by_themselves = false;
  bool server_is_muted_by_admin = false;
  bool server_is_muted_locally = false;

  bool can_be_muted_for_all_users = false;
  bool can_be_unmuted_for_all_users = false;
  bool can_be_muted_only_for_self = false;
  bool can_be_unmuted_only_for_self = false;

  bool is_min = false;
  bool is_fake = false;
  bool is_just_joined = false;
  bool is_speaking = false;
  int32 local_active_date = 0;
  int64 order = 0;

  int32 pending_volume_level = 0;
  uint64 pending_volume_level_generation = 0;

  bool have_pending_is_muted = false;
  bool pending_is_muted_by_themselves = false;
  bool pending_is_muted_by_admin = false;
  bool pending_is_muted_locally = false;
  uint64 pending_is_muted_generation = 0;

  static constexpr int32 MIN_VOLUME_LEVEL = 1;
  static constexpr int32 MAX_VOLUME_LEVEL = 20000;

  GroupCallParticipant() = default;

  explicit GroupCallParticipant(const tl_object_ptr<telegram_api::groupCallParticipant> &participant);

  static bool is_versioned_update(const tl_object_ptr<telegram_api::groupCallParticipant> &participant);

  void update_from(const GroupCallParticipant &old_participant);

  bool update_can_be_muted(bool can_manage, bool is_self, bool is_admin);

  bool set_pending_is_muted(bool is_muted, bool can_manage, bool is_self, bool is_admin);

  int64 get_real_order() const {
    return (static_cast<int64>(max(active_date, local_active_date)) << 32) + joined_date;
  }

  bool is_valid() const {
    return user_id.is_valid();
  }

  bool get_is_muted_by_themselves() const;

  bool get_is_muted_by_admin() const;

  bool get_is_muted_locally() const;

  bool get_is_muted_for_all_users() const;

  int32 get_volume_level() const;

  td_api::object_ptr<td_api::groupCallParticipant> get_group_call_participant_object(
      ContactsManager *contacts_manager) const;
};

bool operator==(const GroupCallParticipant &lhs, const GroupCallParticipant &rhs);

bool operator!=(const GroupCallParticipant &lhs, const GroupCallParticipant &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const GroupCallParticipant &group_call_participant);

}  // namespace td
