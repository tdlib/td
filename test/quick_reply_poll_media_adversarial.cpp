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

TEST(QuickReplyPollMediaAdversarial, ShortcutExportPathRejectsPollMediaBeforeSendabilityChecks) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/QuickReplyManager.cpp");
  auto region =
      extract_region(source,
                     "Result<vector<QuickReplyManager::QuickReplyMessageContent>> "
                     "QuickReplyManager::get_quick_reply_message_contents(",
                     "QuickReplyManager::Shortcut *QuickReplyManager::get_shortcut(QuickReplyShortcutId shortcut_id)");

  const auto guard_pos = region.find("message_content_poll_has_attached_media(message->content.get())");
  const auto can_send_pos = region.find("can_send_message_content(dialog_id, content.get(), false, true, td_)");

  ASSERT_TRUE(guard_pos != td::string::npos);
  ASSERT_TRUE(can_send_pos != td::string::npos);
  ASSERT_TRUE(guard_pos < can_send_pos);
  ASSERT_TRUE(region.find("Can't send polls with media from quick replies") != td::string::npos);
}

}  // namespace
