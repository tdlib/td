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

TEST(DialogParticipantGroupAdminAdversarial, UnknownChannelRightsInjectionCannotEscalateManageTopics) {
  auto rights =
      td::AdministratorRights(false, true, true, false, false, true, true, true, true, true /* can_manage_topics */,
                              false, true, false, false, false, false, true, td::ChannelType::Unknown);

  auto status = td::DialogParticipantStatus::Administrator(std::move(rights), "inject", false);

  ASSERT_TRUE(status.is_administrator());
  ASSERT_FALSE(status.can_edit_topics());
  ASSERT_FALSE(status.can_pin_topics());
  ASSERT_FALSE(status.can_create_topics());
}

TEST(DialogParticipantGroupAdminAdversarial, UnknownChannelTopicScrubMustNotClearManageTags) {
  auto rights = td::AdministratorRights(false, true, true, false, false, true, true, true, true,
                                        true /* attacker-controlled can_manage_topics */, false, true, false, false,
                                        false, false, true /* can_manage_tags */, td::ChannelType::Unknown);

  auto status = td::DialogParticipantStatus::Administrator(std::move(rights), "inject", false);

  ASSERT_TRUE(status.is_administrator());
  ASSERT_FALSE(status.can_edit_topics());
  ASSERT_FALSE(status.can_pin_topics());
  ASSERT_FALSE(status.can_create_topics());
  ASSERT_TRUE(status.can_manage_ranks());

  td::string rank;
  auto status_object = status.get_chat_member_status_object(&rank);
  ASSERT_TRUE(status_object != nullptr);
  ASSERT_EQ(status_object->get_id(), td::td_api::chatMemberStatusAdministrator::ID);

  const auto *administrator = static_cast<const td::td_api::chatMemberStatusAdministrator *>(status_object.get());
  ASSERT_TRUE(administrator->rights_ != nullptr);
  ASSERT_FALSE(administrator->rights_->can_manage_topics_);
  ASSERT_TRUE(administrator->rights_->can_manage_tags_);
}

TEST(DialogParticipantGroupAdminAdversarial, UnknownChannelRightsInjectionCannotRetainChannelOnlyFlags) {
  auto rights = td::AdministratorRights(
      false, true, true, true /* attacker-controlled can_post_messages */,
      true /* attacker-controlled can_edit_messages */, true, true, true, true, true, false, true, true, true,
      true /* attacker-controlled can_manage_direct_messages */, true, true, td::ChannelType::Unknown);

  auto status = td::DialogParticipantStatus::Administrator(std::move(rights), "inject", true);

  ASSERT_TRUE(status.is_administrator());
  ASSERT_FALSE(status.can_post_messages());
  ASSERT_FALSE(status.can_edit_messages());
  ASSERT_FALSE(status.can_manage_direct_messages());
  ASSERT_FALSE(status.can_edit_topics());
  ASSERT_TRUE(status.can_manage_ranks());
}

}  // namespace
