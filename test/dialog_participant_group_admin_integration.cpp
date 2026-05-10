// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/DialogParticipant.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/tests.h"

namespace {

td::telegram_api::object_ptr<td::telegram_api::ChatParticipant> make_admin_participant(td::int64 user_id,
                                                                                       td::int64 inviter_id,
                                                                                       td::int32 date,
                                                                                       td::string rank) {
  auto participant = td::make_tl_object<td::telegram_api::chatParticipantAdmin>();
  participant->user_id_ = user_id;
  participant->inviter_id_ = inviter_id;
  participant->date_ = date;
  participant->rank_ = std::move(rank);
  return participant;
}

TEST(DialogParticipantGroupAdminIntegration,
     ChatParticipantAdminConstructionAndLoadRepairPreserveRankAndLeastPrivilege) {
  auto participant_ptr = make_admin_participant(1001, 42, 1700000001, "security-auditor");
  td::DialogParticipant participant(std::move(participant_ptr), 1700000000, true);

  ASSERT_TRUE(participant.status_.is_administrator());
  ASSERT_TRUE(participant.status_.can_be_edited());
  ASSERT_FALSE(participant.status_.can_promote_members());

  auto repaired_status =
      td::DialogParticipantStatus::GroupAdministrator(false, td::string(participant.status_.get_rank()));

  ASSERT_EQ(repaired_status.get_rank(), participant.status_.get_rank());
  ASSERT_FALSE(repaired_status.can_be_edited());
  ASSERT_FALSE(repaired_status.can_promote_members());
}

TEST(DialogParticipantGroupAdminIntegration, LegacyFactoryRepairDropsCreatorEditabilityWithoutPromoteEscalation) {
  auto status = td::DialogParticipantStatus::GroupAdministrator(true, "lead");
  ASSERT_TRUE(status.can_be_edited());
  ASSERT_FALSE(status.can_promote_members());

  auto repaired_status = td::DialogParticipantStatus::GroupAdministrator(false, td::string(status.get_rank()));

  ASSERT_EQ(repaired_status.get_rank(), status.get_rank());
  ASSERT_FALSE(repaired_status.can_be_edited());
  ASSERT_FALSE(repaired_status.can_promote_members());
}

}  // namespace
