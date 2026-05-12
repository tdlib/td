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

TEST(ForwardedPollStatisticsIntegration, StatisticsManagerQueriesReuseMessageStatisticsAccessGate) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/StatisticsManager.cpp");

  auto message_stats_region = extract_region(source, "void StatisticsManager::send_get_channel_message_stats_query(",
                                             "void StatisticsManager::get_channel_story_statistics(");
  auto public_forwards_region =
      extract_region(source, "void StatisticsManager::send_get_message_public_forwards_query(",
                     "void StatisticsManager::get_story_public_forwards(");

  ASSERT_TRUE(message_stats_region.find("can_get_message_statistics(message_full_id)") != td::string::npos);
  ASSERT_TRUE(public_forwards_region.find("can_get_message_statistics(message_full_id)") != td::string::npos);
}

TEST(ForwardedPollStatisticsIntegration, StatisticsManagerHasDedicatedFailClosedPollStatsPath) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/StatisticsManager.cpp");
  auto region =
      extract_region(source, "void StatisticsManager::get_poll_statistics(MessageFullId message_full_id, bool is_dark,",
                     "void StatisticsManager::load_statistics_graph(DialogId dialog_id, string token, int64 x,");

  ASSERT_TRUE(region.find("have_message_force(message_full_id, \"get_poll_statistics\")") != td::string::npos);
  ASSERT_TRUE(region.find("can_get_message_poll_vote_statistics(message_full_id)") != td::string::npos);
  ASSERT_TRUE(region.find("Poll statistics are inaccessible") != td::string::npos);
  ASSERT_TRUE(region.find("create_handler<GetPollStatsQuery>") != td::string::npos);
}

TEST(ForwardedPollStatisticsIntegration, RequestsRoutePollVoteStatisticsToStatisticsManager) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/Requests.cpp");
  auto region = extract_region(source, "void Requests::on_request(uint64 id, const td_api::getPollVoters &request) {",
                               "void Requests::on_request(uint64 id, td_api::stopPoll &request) {");

  ASSERT_TRUE(region.find("void Requests::on_request(uint64 id, const td_api::getPollVoteStatistics &request)") !=
              td::string::npos);
  ASSERT_TRUE(region.find("td_->statistics_manager_->get_poll_statistics") != td::string::npos);
}

}  // namespace
