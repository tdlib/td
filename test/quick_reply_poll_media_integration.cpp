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

TEST(QuickReplyPollMediaIntegration, RejectMessageAndHelperUsageStayConsistentAcrossAllThreeSeams) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/QuickReplyManager.cpp");

  const auto helper_calls = count_substring_occurrences(source, "message_content_poll_has_media(");
  const auto reject_messages = count_substring_occurrences(source, "Can't send polls with media from quick replies");

  ASSERT_EQ(3u, helper_calls);
  ASSERT_EQ(3u, reject_messages);
}

TEST(QuickReplyPollMediaIntegration, QuickReplySeamsDoNotUseAttachedOnlyHelperAnymore) {
  auto quick_reply_source = td::mtproto::test::read_repo_text_file("td/telegram/QuickReplyManager.cpp");

  const auto old_helper_calls =
      count_substring_occurrences(quick_reply_source, "message_content_poll_has_attached_media(");
  const auto strict_helper_calls = count_substring_occurrences(quick_reply_source, "message_content_poll_has_media(");

  ASSERT_EQ(0u, old_helper_calls);
  ASSERT_EQ(3u, strict_helper_calls);
}

TEST(QuickReplyPollMediaIntegration, PollManagerExposesHasPollForFailClosedGuardPath) {
  auto poll_manager_header = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.h");
  auto poll_manager_source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");

  ASSERT_NE(td::string::npos, poll_manager_header.find("bool has_poll(PollId poll_id) const;"));
  ASSERT_NE(td::string::npos, poll_manager_source.find("bool PollManager::has_poll(PollId poll_id) const {"));
  ASSERT_NE(td::string::npos, poll_manager_source.find("return have_poll(poll_id);"));
}

}  // namespace
