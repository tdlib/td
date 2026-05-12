// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

TEST(DialogParticipantGroupAdminSourceContract, ChatParseRepairPreservesRankForLoadedLegacyAdministrators) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/ChatManager.cpp");
  auto region = extract_region(source, "void ChatManager::Chat::parse(ParserT &parser) {",
                               "template <class StorerT>\nvoid ChatManager::ChatFull::store");

  const auto parse_status_pos = region.find("parse(status, parser);");
  const auto repair_pos =
      region.find("status = DialogParticipantStatus::GroupAdministrator(false, string(status.get_rank()));");

  ASSERT_TRUE(parse_status_pos != td::string::npos);
  ASSERT_TRUE(repair_pos != td::string::npos);
  ASSERT_TRUE(repair_pos > parse_status_pos);
}

TEST(DialogParticipantGroupAdminSourceContract, ChatParseRepairIsScopedToNonCreatorAdministratorsOnly) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/ChatManager.cpp");
  auto region = extract_region(source, "void ChatManager::Chat::parse(ParserT &parser) {",
                               "template <class StorerT>\nvoid ChatManager::ChatFull::store");

  ASSERT_TRUE(region.find("if (status.is_administrator() && !status.is_creator()) {") != td::string::npos);
  ASSERT_TRUE(region.find("GroupAdministrator(false, string(status.get_rank()))") != td::string::npos);
}

TEST(DialogParticipantGroupAdminSourceContract, GroupAdministratorUsesNamedLegacyRightsFactoryProfile) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogParticipant.cpp");
  auto region = extract_region(source, "DialogParticipantStatus DialogParticipantStatus::GroupAdministrator(",
                               "DialogParticipantStatus DialogParticipantStatus::ChannelAdministrator(");

  ASSERT_TRUE(region.find("legacy_group_administrator_rights()") != td::string::npos);
}

TEST(DialogParticipantGroupAdminSourceContract, UnknownChannelAdministratorRightsFailClosedOnManageTopics) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogParticipant.cpp");
  auto region = extract_region(source, "AdministratorRights::AdministratorRights(bool is_anonymous",
                               "telegram_api::object_ptr<telegram_api::chatAdminRights>");

  const auto unknown_branch_pos = region.find("case ChannelType::Unknown:");
  const auto clear_topics_pos = unknown_branch_pos == td::string::npos
                                    ? td::string::npos
                                    : region.find("can_manage_topics = false;", unknown_branch_pos);

  ASSERT_TRUE(unknown_branch_pos != td::string::npos);
  ASSERT_TRUE(clear_topics_pos != td::string::npos);
  ASSERT_TRUE(clear_topics_pos > unknown_branch_pos);
}

TEST(DialogParticipantGroupAdminSourceContract, UnknownChannelAdministratorRightsFailClosedOnChannelOnlyFlags) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogParticipant.cpp");
  auto region = extract_region(source, "AdministratorRights::AdministratorRights(bool is_anonymous",
                               "telegram_api::object_ptr<telegram_api::chatAdminRights>");

  const auto unknown_branch_pos = region.find("case ChannelType::Unknown:");
  const auto clear_post_pos = unknown_branch_pos == td::string::npos
                                  ? td::string::npos
                                  : region.find("can_post_messages = false;", unknown_branch_pos);
  const auto clear_edit_pos = unknown_branch_pos == td::string::npos
                                  ? td::string::npos
                                  : region.find("can_edit_messages = false;", unknown_branch_pos);
  const auto clear_direct_messages_pos = unknown_branch_pos == td::string::npos
                                             ? td::string::npos
                                             : region.find("can_manage_direct_messages = false;", unknown_branch_pos);

  ASSERT_TRUE(unknown_branch_pos != td::string::npos);
  ASSERT_TRUE(clear_post_pos != td::string::npos);
  ASSERT_TRUE(clear_edit_pos != td::string::npos);
  ASSERT_TRUE(clear_direct_messages_pos != td::string::npos);
  ASSERT_TRUE(clear_post_pos > unknown_branch_pos);
  ASSERT_TRUE(clear_edit_pos > unknown_branch_pos);
  ASSERT_TRUE(clear_direct_messages_pos > unknown_branch_pos);
}

}  // namespace
