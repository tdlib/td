// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/DialogParticipant.h"
#include "td/telegram/td_api.h"

#include "td/utils/tests.h"

namespace {

td::td_api::object_ptr<td::td_api::chatPermissions> make_permissions_for_unknown_channel_injection(bool can_edit_tag) {
  return td::td_api::make_object<td::td_api::chatPermissions>(false, true, true, true, true, true, true, true, true,
                                                              true, can_edit_tag, true, true, true,
                                                              true /* attacker-controlled can_create_topics */);
}

TEST(DialogParticipantRestrictedRightsIntegration,
     UnknownChannelTdApiRestrictedParsingDropsCreateTopicsAndKeepsEditTag) {
  auto permissions = make_permissions_for_unknown_channel_injection(true);
  auto status = td::DialogParticipantStatus::Restricted(td::RestrictedRights(permissions, td::ChannelType::Unknown),
                                                        true, 0, td::ChannelType::Unknown, "restricted");

  ASSERT_TRUE(status.is_restricted());
  ASSERT_FALSE(status.can_create_topics());
  ASSERT_TRUE(status.can_edit_rank());

  auto exported = status.get_chat_member_status_object(nullptr);
  ASSERT_TRUE(exported != nullptr);
  ASSERT_EQ(exported->get_id(), td::td_api::chatMemberStatusRestricted::ID);

  const auto *restricted = static_cast<const td::td_api::chatMemberStatusRestricted *>(exported.get());
  ASSERT_TRUE(restricted->permissions_ != nullptr);
  ASSERT_FALSE(restricted->permissions_->can_create_topics_);
  ASSERT_TRUE(restricted->permissions_->can_edit_tag_);
}

TEST(DialogParticipantRestrictedRightsIntegration,
     UnknownChannelTdApiRestrictedParsingKeepsEditTagDisabledWhenInputDisablesIt) {
  auto permissions = make_permissions_for_unknown_channel_injection(false);
  auto status = td::DialogParticipantStatus::Restricted(td::RestrictedRights(permissions, td::ChannelType::Unknown),
                                                        true, 0, td::ChannelType::Unknown, "restricted");

  ASSERT_TRUE(status.is_restricted());
  ASSERT_FALSE(status.can_create_topics());
  ASSERT_FALSE(status.can_edit_rank());
}

}  // namespace
