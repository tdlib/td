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

TEST(ForwardedPollStatisticsAdversarial, ForwardedPollHelperRemainsFailClosedForNonPollOrImportedOrigins) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source, "bool MessagesManager::can_get_message_statistics(DialogId dialog_id, const Message *m) const {",
      "bool MessagesManager::can_get_message_author(DialogId dialog_id, const Message *m) const {");

  ASSERT_TRUE(region.find("content->get_type() == MessageContentType::Poll") != td::string::npos);
  ASSERT_TRUE(region.find("forward_info != nullptr") != td::string::npos);
  ASSERT_TRUE(region.find("!message->forward_info->is_imported()") != td::string::npos);
  ASSERT_TRUE(region.find("forward_info->get_origin().is_channel_post()") != td::string::npos);
  ASSERT_TRUE(region.find("if (is_forwarded_message && !can_get_forwarded_poll_statistics(m))") != td::string::npos);
}

}  // namespace
