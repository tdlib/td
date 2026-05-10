// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/DialogParticipant.h"
#include "td/telegram/td_api.h"

#include "td/utils/tests.h"

namespace {

TEST(DialogParticipantGroupAdminContract, GroupAdministratorNeverGrantsPromoteMembersRight) {
  const auto status_when_current_user_is_creator = td::DialogParticipantStatus::GroupAdministrator(true, "admin");
  const auto status_when_current_user_is_not_creator = td::DialogParticipantStatus::GroupAdministrator(false, "admin");

  ASSERT_FALSE(status_when_current_user_is_creator.can_promote_members());
  ASSERT_FALSE(status_when_current_user_is_not_creator.can_promote_members());
}

TEST(DialogParticipantGroupAdminContract, GroupAdministratorCanBeEditedTracksCurrentUserCreatorState) {
  const auto status_when_current_user_is_creator = td::DialogParticipantStatus::GroupAdministrator(true, "admin");
  const auto status_when_current_user_is_not_creator = td::DialogParticipantStatus::GroupAdministrator(false, "admin");

  ASSERT_TRUE(status_when_current_user_is_creator.can_be_edited());
  ASSERT_FALSE(status_when_current_user_is_not_creator.can_be_edited());
}

TEST(DialogParticipantGroupAdminContract, GroupAdministratorExportsAdministratorObjectWithoutPromoteRight) {
  const auto status = td::DialogParticipantStatus::GroupAdministrator(true, "lead");

  td::string rank;
  auto status_object = status.get_chat_member_status_object(&rank);
  ASSERT_TRUE(status_object != nullptr);
  ASSERT_EQ(status_object->get_id(), td::td_api::chatMemberStatusAdministrator::ID);
  ASSERT_EQ(rank, "lead");

  const auto *administrator = static_cast<const td::td_api::chatMemberStatusAdministrator *>(status_object.get());
  ASSERT_TRUE(administrator->rights_ != nullptr);
  ASSERT_FALSE(administrator->rights_->can_promote_members_);
}

TEST(DialogParticipantGroupAdminContract, SetRankPreservesFactorySanitizationInvariant) {
  auto status = td::DialogParticipantStatus::GroupAdministrator(false, "seed");

  ASSERT_TRUE(status.set_rank(td::string(32, 'x')));
  ASSERT_EQ(status.get_rank(), td::string(16, 'x'));

  ASSERT_TRUE(status.set_rank("\n\t "));
  ASSERT_EQ(status.get_rank(), td::string());
}

}  // namespace
