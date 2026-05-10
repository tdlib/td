// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

td::size_t count_substring_occurrences(const td::string &source, td::Slice needle) {
  td::size_t count = 0;
  td::size_t pos = 0;
  while (true) {
    pos = source.find(needle.str(), pos);
    if (pos == td::string::npos) {
      return count;
    }
    count++;
    pos += needle.size();
  }
}

TEST(QuickReplyPollMediaIntegration, RejectMessageAndHelperUsageStayConsistentAcrossBothSeams) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/QuickReplyManager.cpp");

  const auto helper_calls = count_substring_occurrences(source, "message_content_poll_has_attached_media(");
  const auto reject_messages = count_substring_occurrences(source, "Can't send polls with media from quick replies");

  ASSERT_TRUE(helper_calls >= 2u);
  ASSERT_EQ(2u, reject_messages);
}

}  // namespace
