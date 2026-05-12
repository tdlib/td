// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/DialogParticipant.h"

#include "td/utils/tests.h"

#include <cstdint>

namespace {

TEST(DialogParticipantRestrictedRightsStress, UnknownChannelNeverLeaksCreateTopicsUnderHighVolumeRightsConstruction) {
  constexpr std::int32_t kIterations = 250000;

  for (std::int32_t i = 0; i < kIterations; ++i) {
    const bool alternate = (i % 2) == 0;
    auto rights = td::RestrictedRights(false, true, true, true, true, true, true, true, true, true, true, true, true,
                                       true, true, true, true /* attacker-controlled can_manage_topics */, alternate,
                                       td::ChannelType::Unknown);

    auto status =
        td::DialogParticipantStatus::Restricted(std::move(rights), true, 0, td::ChannelType::Unknown, "stress");

    ASSERT_TRUE(status.is_restricted());
    ASSERT_FALSE(status.can_create_topics());
    ASSERT_EQ(status.can_edit_rank(), alternate);
  }
}

}  // namespace
