// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    auto byte = static_cast<unsigned char>(c);
    if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

TEST(DialogParticipantRestrictedRightsSourceContract, UnknownChannelRestrictedRightsFailClosedOnManageTopics) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogParticipant.cpp");
  auto normalized = normalize_for_contract(source);

  const auto constructor_pos = normalized.find("RestrictedRights::RestrictedRights(boolcan_send_messages,");
  const auto unknown_branch_pos = constructor_pos == td::string::npos
                                      ? td::string::npos
                                      : normalized.find("caseChannelType::Unknown:", constructor_pos);
  const auto clear_topics_pos = unknown_branch_pos == td::string::npos
                                    ? td::string::npos
                                    : normalized.find("can_manage_topics=false;", unknown_branch_pos);

  ASSERT_TRUE(constructor_pos != td::string::npos);
  ASSERT_TRUE(unknown_branch_pos != td::string::npos);
  ASSERT_TRUE(clear_topics_pos != td::string::npos);
  ASSERT_TRUE(clear_topics_pos > unknown_branch_pos);
}

TEST(DialogParticipantRestrictedRightsSourceContract, RestrictedStatusParsingMustFlowThroughRestrictedRightsBoundary) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogParticipant.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("DialogParticipantStatus::Restricted(RestrictedRights(st->permissions_,channel_type),"
                              "st->is_member_,fix_until_date(st->restricted_until_date_),channel_type,string());") !=
              td::string::npos);
}

TEST(DialogParticipantRestrictedRightsSourceContract,
     ChatParseMustNormalizeUnknownDefaultPermissionsToNoTopicsProfile) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/ChatManager.cpp");
  auto normalized = normalize_for_contract(source);

  const auto condition_pos = normalized.find("if(default_permissions.can_manage_topics()){");
  const auto reconstruct_pos = condition_pos == td::string::npos
                                   ? td::string::npos
                                   : normalized.find("default_permissions=RestrictedRights(", condition_pos);
  const auto false_topics_pos =
      condition_pos == td::string::npos
          ? td::string::npos
          : normalized.find("false,default_permissions.can_edit_rank(),ChannelType::Unknown);", condition_pos);

  ASSERT_TRUE(condition_pos != td::string::npos);
  ASSERT_TRUE(reconstruct_pos != td::string::npos);
  ASSERT_TRUE(false_topics_pos != td::string::npos);
  ASSERT_TRUE(condition_pos < reconstruct_pos);
  ASSERT_TRUE(reconstruct_pos < false_topics_pos);
}

}  // namespace
