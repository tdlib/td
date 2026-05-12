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

TEST(DialogParticipantGroupAdminContract, UnknownChannelAdministratorRightsIgnoreManageTopicsFlag) {
  auto rights =
      td::AdministratorRights(false, true, true, false, false, true, true, true, true, true /* can_manage_topics */,
                              false, true, false, false, false, false, true, td::ChannelType::Unknown);

  auto status = td::DialogParticipantStatus::Administrator(std::move(rights), "topic-inject", true);

  ASSERT_FALSE(status.can_edit_topics());
  ASSERT_FALSE(status.can_pin_topics());
  ASSERT_FALSE(status.can_create_topics());

  td::string rank;
  auto status_object = status.get_chat_member_status_object(&rank);
  ASSERT_TRUE(status_object != nullptr);
  ASSERT_EQ(status_object->get_id(), td::td_api::chatMemberStatusAdministrator::ID);

  const auto *administrator = static_cast<const td::td_api::chatMemberStatusAdministrator *>(status_object.get());
  ASSERT_TRUE(administrator->rights_ != nullptr);
  ASSERT_FALSE(administrator->rights_->can_manage_topics_);
}

TEST(DialogParticipantGroupAdminContract, GroupAdministratorExportsManageTagsForBasicGroups) {
  const auto status = td::DialogParticipantStatus::GroupAdministrator(false, "tag-admin");

  ASSERT_TRUE(status.can_manage_ranks());
  ASSERT_FALSE(status.can_promote_members());

  td::string rank;
  auto status_object = status.get_chat_member_status_object(&rank);
  ASSERT_TRUE(status_object != nullptr);
  ASSERT_EQ(status_object->get_id(), td::td_api::chatMemberStatusAdministrator::ID);
  ASSERT_EQ(rank, "tag-admin");

  const auto *administrator = static_cast<const td::td_api::chatMemberStatusAdministrator *>(status_object.get());
  ASSERT_TRUE(administrator->rights_ != nullptr);
  ASSERT_TRUE(administrator->rights_->can_manage_tags_);
  ASSERT_FALSE(administrator->rights_->can_manage_topics_);
  ASSERT_FALSE(administrator->rights_->can_promote_members_);
}

TEST(DialogParticipantGroupAdminContract,
     UnknownChannelAdministratorRightsStripChannelOnlyFlagsWhilePreservingManageTags) {
  auto rights = td::AdministratorRights(false, true, true, true, true, true, true, true, true, true, false, true, true,
                                        true, true, true, true, td::ChannelType::Unknown);

  auto status = td::DialogParticipantStatus::Administrator(std::move(rights), "unknown-injected", true);

  ASSERT_TRUE(status.is_administrator());
  ASSERT_FALSE(status.can_post_messages());
  ASSERT_FALSE(status.can_edit_messages());
  ASSERT_FALSE(status.can_manage_direct_messages());
  ASSERT_FALSE(status.can_edit_topics());
  ASSERT_TRUE(status.can_manage_ranks());

  td::string rank;
  auto status_object = status.get_chat_member_status_object(&rank);
  ASSERT_TRUE(status_object != nullptr);
  ASSERT_EQ(status_object->get_id(), td::td_api::chatMemberStatusAdministrator::ID);

  const auto *administrator = static_cast<const td::td_api::chatMemberStatusAdministrator *>(status_object.get());
  ASSERT_TRUE(administrator->rights_ != nullptr);
  ASSERT_FALSE(administrator->rights_->can_post_messages_);
  ASSERT_FALSE(administrator->rights_->can_edit_messages_);
  ASSERT_FALSE(administrator->rights_->can_manage_direct_messages_);
  ASSERT_FALSE(administrator->rights_->can_manage_topics_);
  ASSERT_TRUE(administrator->rights_->can_manage_tags_);
}

}  // namespace
