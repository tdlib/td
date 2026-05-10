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

}  // namespace
