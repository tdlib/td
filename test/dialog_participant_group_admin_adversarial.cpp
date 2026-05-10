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

TEST(DialogParticipantGroupAdminAdversarial, CreatorContextDoesNotEscalatePromoteMembersRightForLoadedAdmins) {
  auto participant_ptr = make_admin_participant(1001, 42, 1700000001, "mod");
  td::DialogParticipant participant(std::move(participant_ptr), 1700000000, true);

  ASSERT_TRUE(participant.status_.is_administrator());
  ASSERT_TRUE(participant.status_.can_be_edited());
  ASSERT_FALSE(participant.status_.can_promote_members());
}

TEST(DialogParticipantGroupAdminAdversarial, NonCreatorContextKeepsAdminNonEditableAndNoPromoteRight) {
  auto participant_ptr = make_admin_participant(1002, 42, 1700000002, "mod");
  td::DialogParticipant participant(std::move(participant_ptr), 1700000000, false);

  ASSERT_TRUE(participant.status_.is_administrator());
  ASSERT_FALSE(participant.status_.can_be_edited());
  ASSERT_FALSE(participant.status_.can_promote_members());
}

TEST(DialogParticipantGroupAdminAdversarial, CreatorContextKeepsRankWhileBlockingPromoteEscalation) {
  auto participant_ptr = make_admin_participant(1003, 42, 1700000003, "security-auditor");
  td::DialogParticipant participant(std::move(participant_ptr), 1700000000, true);

  ASSERT_EQ(participant.status_.get_rank(), "security-auditor");
  ASSERT_FALSE(participant.status_.can_promote_members());
}

}  // namespace
