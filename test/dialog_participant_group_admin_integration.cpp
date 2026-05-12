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

td::td_api::object_ptr<td::td_api::chatAdministratorRights> make_admin_rights_for_unknown_channel_injection() {
  return td::td_api::make_object<td::td_api::chatAdministratorRights>(
      true, true, true, true, true, true, true, true, true, false, true, true, true, true, true, true, false);
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

TEST(DialogParticipantGroupAdminIntegration, UnknownChannelTlAdminRightsDoNotEnableTopicManagement) {
  auto admin_rights = td::make_tl_object<td::telegram_api::chatAdminRights>();
  admin_rights->other_ = true;
  admin_rights->change_info_ = true;
  admin_rights->delete_messages_ = true;
  admin_rights->invite_users_ = true;
  admin_rights->ban_users_ = true;
  admin_rights->pin_messages_ = true;
  admin_rights->manage_call_ = true;
  admin_rights->manage_ranks_ = true;
  admin_rights->manage_topics_ = true;

  auto status = td::DialogParticipantStatus(true, std::move(admin_rights), "lead", td::ChannelType::Unknown);

  ASSERT_TRUE(status.is_administrator());
  ASSERT_FALSE(status.can_edit_topics());
  ASSERT_FALSE(status.can_pin_topics());
  ASSERT_FALSE(status.can_create_topics());
}

TEST(DialogParticipantGroupAdminIntegration,
     GroupAdministratorObjectRoundTripPreservesManageTagsWithoutTopicPrivilege) {
  const auto status = td::DialogParticipantStatus::GroupAdministrator(false, "tag-admin");
  td::string rank;
  auto status_object = status.get_chat_member_status_object(&rank);

  ASSERT_TRUE(status_object != nullptr);
  ASSERT_EQ(status_object->get_id(), td::td_api::chatMemberStatusAdministrator::ID);

  const auto *administrator = static_cast<const td::td_api::chatMemberStatusAdministrator *>(status_object.get());
  ASSERT_TRUE(administrator->rights_ != nullptr);
  ASSERT_TRUE(administrator->rights_->can_manage_tags_);
  ASSERT_FALSE(administrator->rights_->can_manage_topics_);
  ASSERT_FALSE(administrator->rights_->can_promote_members_);

  auto reparsed_status = td::DialogParticipantStatus::Administrator(
      td::AdministratorRights(administrator->rights_, td::ChannelType::Unknown), td::string(rank),
      administrator->can_be_edited_);

  ASSERT_TRUE(reparsed_status.is_administrator());
  ASSERT_TRUE(reparsed_status.can_manage_ranks());
  ASSERT_FALSE(reparsed_status.can_edit_topics());
  ASSERT_FALSE(reparsed_status.can_pin_topics());
  ASSERT_FALSE(reparsed_status.can_create_topics());
  ASSERT_FALSE(reparsed_status.can_promote_members());
}

TEST(DialogParticipantGroupAdminIntegration,
     UnknownChannelTdApiAdministratorParsingDropsChannelOnlyFlagsAndKeepsManageTags) {
  auto status_object = td::td_api::make_object<td::td_api::chatMemberStatusAdministrator>(
      true, make_admin_rights_for_unknown_channel_injection());

  auto status = td::get_dialog_participant_status(std::move(status_object), td::ChannelType::Unknown);

  ASSERT_TRUE(status.is_administrator());
  ASSERT_FALSE(status.can_post_messages());
  ASSERT_FALSE(status.can_edit_messages());
  ASSERT_FALSE(status.can_manage_direct_messages());
  ASSERT_FALSE(status.can_edit_topics());
  ASSERT_TRUE(status.can_manage_ranks());

  td::string rank;
  auto exported = status.get_chat_member_status_object(&rank);
  ASSERT_TRUE(exported != nullptr);
  ASSERT_EQ(exported->get_id(), td::td_api::chatMemberStatusAdministrator::ID);

  const auto *administrator = static_cast<const td::td_api::chatMemberStatusAdministrator *>(exported.get());
  ASSERT_TRUE(administrator->rights_ != nullptr);
  ASSERT_FALSE(administrator->rights_->can_post_messages_);
  ASSERT_FALSE(administrator->rights_->can_edit_messages_);
  ASSERT_FALSE(administrator->rights_->can_manage_direct_messages_);
  ASSERT_FALSE(administrator->rights_->can_manage_topics_);
  ASSERT_TRUE(administrator->rights_->can_manage_tags_);
}

}  // namespace
