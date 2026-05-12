// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/DialogParticipant.h"
#include "td/telegram/td_api.h"

#include "td/utils/tests.h"

namespace {

TEST(DialogParticipantRestrictedRightsContract, UnknownChannelRestrictedRightsIgnoreManageTopicsFlag) {
  auto rights =
      td::RestrictedRights(false, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
                           true, true /* attacker-controlled can_manage_topics */, true, td::ChannelType::Unknown);

  auto status = td::DialogParticipantStatus::Restricted(std::move(rights), true, 0, td::ChannelType::Unknown, "ranked");

  ASSERT_TRUE(status.is_restricted());
  ASSERT_FALSE(status.can_create_topics());
  ASSERT_TRUE(status.can_edit_rank());
}

TEST(DialogParticipantRestrictedRightsContract,
     UnknownChannelRestrictedRightsExportKeepsEditTagWithoutTopicsPrivilege) {
  auto rights =
      td::RestrictedRights(false, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
                           true, true /* attacker-controlled can_manage_topics */, true, td::ChannelType::Unknown);

  auto status = td::DialogParticipantStatus::Restricted(std::move(rights), true, 0, td::ChannelType::Unknown, "ranked");

  auto status_object = status.get_chat_member_status_object(nullptr);
  ASSERT_TRUE(status_object != nullptr);
  ASSERT_EQ(status_object->get_id(), td::td_api::chatMemberStatusRestricted::ID);

  const auto *restricted = static_cast<const td::td_api::chatMemberStatusRestricted *>(status_object.get());
  ASSERT_TRUE(restricted->permissions_ != nullptr);
  ASSERT_FALSE(restricted->permissions_->can_create_topics_);
  ASSERT_TRUE(restricted->permissions_->can_edit_tag_);
}

}  // namespace
