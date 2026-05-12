// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/DialogParticipant.h"

#include "td/utils/tests.h"

namespace {

TEST(DialogParticipantRestrictedRightsAdversarial, UnknownChannelRightsInjectionCannotRetainCreateTopicsPrivilege) {
  auto rights =
      td::RestrictedRights(false, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
                           true, true /* attacker-controlled can_manage_topics */, false, td::ChannelType::Unknown);

  auto status = td::DialogParticipantStatus::Restricted(std::move(rights), true, 0, td::ChannelType::Unknown, "inject");

  ASSERT_TRUE(status.is_restricted());
  ASSERT_FALSE(status.can_create_topics());
}

TEST(DialogParticipantRestrictedRightsAdversarial, UnknownChannelTopicScrubMustNotClearEditTagPrivilege) {
  auto rights =
      td::RestrictedRights(false, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
                           true, true /* attacker-controlled can_manage_topics */, true, td::ChannelType::Unknown);

  auto status = td::DialogParticipantStatus::Restricted(std::move(rights), true, 0, td::ChannelType::Unknown, "inject");

  ASSERT_TRUE(status.is_restricted());
  ASSERT_FALSE(status.can_create_topics());
  ASSERT_TRUE(status.can_edit_rank());
}

}  // namespace
