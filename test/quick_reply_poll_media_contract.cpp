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
    auto byte = static_cast<unsigned char>(c);
    if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

TEST(QuickReplyPollMediaContract, DoSendMessageRejectsPollsWithAnyMediaFailClosed) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/QuickReplyManager.cpp");
  auto region = extract_region(source, "void QuickReplyManager::do_send_message(",
                               "void QuickReplyManager::on_send_message_file_error(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(message_content_poll_has_media(content,td_)){") != td::string::npos);
  ASSERT_TRUE(normalized.find("Can'tsendpollswithmediafromquickreplies") != td::string::npos);
}

TEST(QuickReplyPollMediaContract, ShortcutExportPathUsesTheSameAnyMediaGuard) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/QuickReplyManager.cpp");
  auto region = extract_region(source,
                               "Result<vector<QuickReplyManager::QuickReplyMessageContent>> "
                               "QuickReplyManager::get_quick_reply_message_contents(",
                               "QuickReplyManager::Shortcut *QuickReplyManager::get_shortcut("
                               "QuickReplyShortcutId shortcut_id)");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(message_content_poll_has_media(message->content.get(),td_)){") != td::string::npos);
  ASSERT_TRUE(normalized.find("Can'tsendpollswithmediafromquickreplies") != td::string::npos);
}

TEST(QuickReplyPollMediaContract, ProcessInputMessageContentRejectsPollsWithAnyMediaBeforeReturn) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/QuickReplyManager.cpp");
  auto region = extract_region(source, "Result<InputMessageContent> QuickReplyManager::process_input_message_content(",
                               "MessageId QuickReplyManager::get_next_message_id(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(
      normalized.find(
          "TRY_RESULT(content,get_input_message_content(DialogId(),std::move(input_message_content),td_,true));") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find("if(message_content_poll_has_media(content.content.get(),td_)){") != td::string::npos);
  ASSERT_TRUE(normalized.find("Can'tsendpollswithmediafromquickreplies") != td::string::npos);
}

TEST(QuickReplyPollMediaContract, PollMediaHelperFailClosesOnMissingPollManagerOrPollState) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");
  auto region =
      extract_region(source, "bool message_content_poll_has_media(const MessageContent *content, const Td *td)",
                     "bool get_message_content_poll_is_anonymous(const Td *td, const MessageContent *content)");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(td==nullptr){") != td::string::npos);
  ASSERT_TRUE(normalized.find("auto*poll_manager=get_poll_manager_for_content_access(td,poll->poll_id,"
                              "\"message_content_poll_has_media\");") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(poll_manager==nullptr){returntrue;}") != td::string::npos);
  ASSERT_TRUE(normalized.find("return!poll_manager->get_poll_file_ids(poll->poll_id).empty();") != td::string::npos);
}

TEST(QuickReplyPollMediaContract, PollFileIdExtractionGuardsUnknownPollStateBeforeLookup) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");
  auto region =
      extract_region(source,
                     "vector<FileId> get_message_content_file_ids(const MessageContent *content, "
                     "const Td *td) {",
                     "StoryFullId get_message_content_story_full_id(const Td *td, const MessageContent *content)");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("auto*poll_manager=get_poll_manager_for_content_access(td,poll->poll_id,"
                              "\"get_message_content_file_ids\");") != td::string::npos);
  ASSERT_TRUE(normalized.find("poll_manager->get_poll_file_ids(poll->poll_id);") != td::string::npos);
}

}  // namespace
