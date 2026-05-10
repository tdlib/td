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

}  // namespace
