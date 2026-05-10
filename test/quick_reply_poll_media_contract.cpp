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

TEST(QuickReplyPollMediaContract, DoSendMessageRejectsPollsWithAttachedMediaFailClosed) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/QuickReplyManager.cpp");
  auto region = extract_region(source, "void QuickReplyManager::do_send_message(",
                               "void QuickReplyManager::on_send_message_file_error(");

  ASSERT_TRUE(region.find("message_content_poll_has_attached_media(content)") != td::string::npos);
  ASSERT_TRUE(region.find("Can't send polls with media from quick replies") != td::string::npos);
}

}  // namespace
