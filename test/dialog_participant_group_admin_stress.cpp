// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/DialogParticipant.h"

#include "td/utils/tests.h"

#include <cstdint>

namespace {

TEST(DialogParticipantGroupAdminStress, FactoryRemainsStableUnderHighVolumeConstruction) {
  constexpr std::int32_t kIterations = 250000;

  for (std::int32_t i = 0; i < kIterations; ++i) {
    const bool is_current_user_creator = (i % 2) == 0;
    const td::string rank = (i % 3) == 0 ? "lead" : "mod";

    auto status = td::DialogParticipantStatus::GroupAdministrator(is_current_user_creator, td::string(rank));

    ASSERT_TRUE(status.is_administrator());
    ASSERT_FALSE(status.can_promote_members());
    ASSERT_TRUE(status.can_manage_ranks());
    ASSERT_EQ(status.can_be_edited(), is_current_user_creator);
    ASSERT_EQ(status.get_rank(), rank);
  }
}

TEST(DialogParticipantGroupAdminStress, UnknownChannelNeverLeaksManageTopicsUnderHighVolumeRightsConstruction) {
  constexpr std::int32_t kIterations = 250000;

  for (std::int32_t i = 0; i < kIterations; ++i) {
    const bool alternate = (i % 2) == 0;
    auto rights = td::AdministratorRights(false, true, true, false, false, true, true, true, true,
                                          true /* force manage_topics in attacker-controlled payload */, alternate,
                                          true, false, false, false, false, true, td::ChannelType::Unknown);

    auto status = td::DialogParticipantStatus::Administrator(std::move(rights), "stress", alternate);

    ASSERT_FALSE(status.can_edit_topics());
    ASSERT_FALSE(status.can_pin_topics());
    ASSERT_FALSE(status.can_create_topics());
    ASSERT_TRUE(status.can_manage_ranks());
  }
}

TEST(DialogParticipantGroupAdminStress, UnknownChannelNeverLeaksChannelOnlyFlagsUnderHighVolumeRightsConstruction) {
  constexpr std::int32_t kIterations = 250000;

  for (std::int32_t i = 0; i < kIterations; ++i) {
    const bool alternate = (i % 2) == 0;
    auto rights = td::AdministratorRights(false, true, true, true, true, true, true, true, true, true, alternate, true,
                                          true, true, true, true, true, td::ChannelType::Unknown);

    auto status = td::DialogParticipantStatus::Administrator(std::move(rights), "stress", alternate);

    ASSERT_FALSE(status.can_post_messages());
    ASSERT_FALSE(status.can_edit_messages());
    ASSERT_FALSE(status.can_manage_direct_messages());
    ASSERT_FALSE(status.can_edit_topics());
    ASSERT_FALSE(status.can_pin_topics());
    ASSERT_FALSE(status.can_create_topics());
    ASSERT_TRUE(status.can_manage_ranks());
  }
}

}  // namespace
