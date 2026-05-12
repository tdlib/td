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

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

TEST(ForwardedPollStatisticsContract, MessageStatisticsGateUsesForwardedPollHelper) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source, "bool MessagesManager::can_get_message_statistics(DialogId dialog_id, const Message *m) const {",
      "bool MessagesManager::can_get_message_author(DialogId dialog_id, const Message *m) const {");

  ASSERT_TRUE(region.find("can_get_forwarded_poll_statistics") != td::string::npos);
  ASSERT_TRUE(region.find("is_forwarded_message") != td::string::npos);
}

TEST(ForwardedPollStatisticsContract, MessageStatisticsGateTreatsAnyRetainedForwardInfoAsForwarded) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source, "bool MessagesManager::can_get_message_statistics(DialogId dialog_id, const Message *m) const {",
      "bool MessagesManager::can_get_message_author(DialogId dialog_id, const Message *m) const {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("constboolis_forwarded_message=m->forward_info!=nullptr||m->had_forward_info;") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("if(is_forwarded_message&&!can_get_forwarded_poll_statistics(m)){") != td::string::npos);
}

TEST(ForwardedPollStatisticsContract, MessagePropertiesExposePollVoteStatisticsCapabilityFlag) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region =
      extract_region(source, "void MessagesManager::get_message_properties(DialogId dialog_id, MessageId message_id,",
                     "void MessagesManager::get_poll_option_properties(DialogId dialog_id, MessageId message_id,");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("autocan_get_poll_vote_statistics=can_get_message_poll_vote_statistics(dialog_id,m);") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find(
                  "can_get_link,can_get_media_timestamp_links,can_get_message_thread,can_get_poll_vote_statistics,") !=
              td::string::npos);
}

TEST(ForwardedPollStatisticsContract, DedicatedPollVoteStatisticsGateRequiresPollVisibilitySignal) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source, "bool MessagesManager::can_get_message_poll_vote_statistics(MessageFullId message_full_id) {",
      "bool MessagesManager::can_get_message_author(DialogId dialog_id, const Message *m) const {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("m->content->get_type()!=MessageContentType::Poll") != td::string::npos);
  ASSERT_TRUE(normalized.find("!td_->dialog_manager_->have_input_peer(dialog_id,false,AccessRights::Read)") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("returnget_message_content_poll_can_view_stats(td_,m->content.get());") !=
              td::string::npos);
}

TEST(ForwardedPollStatisticsContract, TdApiSchemaDeclaresPollVoteStatisticsContract) {
  auto source = td::mtproto::test::read_repo_text_file("td/generate/scheme/td_api.tl");

  ASSERT_TRUE(source.find("can_get_poll_vote_statistics:Bool") != td::string::npos);
  ASSERT_TRUE(source.find("pollVoteStatistics vote_graph:StatisticalGraph = PollVoteStatistics;") != td::string::npos);
  ASSERT_TRUE(source.find("getPollVoteStatistics chat_id:int53 message_id:int53 is_dark:Bool = PollVoteStatistics;") !=
              td::string::npos);
}

}  // namespace
