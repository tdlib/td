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

TEST(QuickReplyPollMediaAdversarial, ShortcutExportPathRejectsPollMediaBeforeSendabilityChecks) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/QuickReplyManager.cpp");
  auto region =
      extract_region(source,
                     "Result<vector<QuickReplyManager::QuickReplyMessageContent>> "
                     "QuickReplyManager::get_quick_reply_message_contents(",
                     "QuickReplyManager::Shortcut *QuickReplyManager::get_shortcut(QuickReplyShortcutId shortcut_id)");

  const auto guard_pos = region.find("message_content_poll_has_media(message->content.get(), td_)");
  const auto can_send_pos = region.find("can_send_message_content(dialog_id, content.get(), false, true, td_)");
  const auto dup_pos = region.find("dup_message_content(td_, dialog_id, message->content.get()",
                                   guard_pos == td::string::npos ? 0 : guard_pos);

  ASSERT_TRUE(guard_pos != td::string::npos);
  ASSERT_TRUE(dup_pos != td::string::npos);
  ASSERT_TRUE(can_send_pos != td::string::npos);
  ASSERT_TRUE(guard_pos < dup_pos);
  ASSERT_TRUE(guard_pos < can_send_pos);
  ASSERT_TRUE(region.find("Can't send polls with media from quick replies") != td::string::npos);
}

TEST(QuickReplyPollMediaAdversarial, ProcessInputMessageContentRejectsPollMediaBeforeReturningConvertedContent) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/QuickReplyManager.cpp");
  auto region = extract_region(source, "Result<InputMessageContent> QuickReplyManager::process_input_message_content(",
                               "MessageId QuickReplyManager::get_next_message_id(");

  const auto convert_pos = region.find(
      "TRY_RESULT(content, get_input_message_content(DialogId(), std::move(input_message_content), td_, true));");
  const auto guard_pos = region.find("message_content_poll_has_media(content.content.get(), td_)");
  const auto return_pos = region.find("return std::move(content);");

  ASSERT_TRUE(convert_pos != td::string::npos);
  ASSERT_TRUE(guard_pos != td::string::npos);
  ASSERT_TRUE(return_pos != td::string::npos);
  ASSERT_TRUE(convert_pos < guard_pos);
  ASSERT_TRUE(guard_pos < return_pos);
  ASSERT_TRUE(region.find("Can't send polls with media from quick replies") != td::string::npos);
}

TEST(QuickReplyPollMediaAdversarial, PollMediaHelperChecksAttachedAndPollStateMediaFailClosed) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");
  auto region =
      extract_region(source, "bool message_content_poll_has_media(const MessageContent *content, const Td *td)",
                     "bool get_message_content_poll_is_anonymous(const Td *td, const MessageContent *content)");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(content==nullptr){returnfalse;}") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(content->get_type()!=MessageContentType::Poll){returnfalse;}") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(td==nullptr){") != td::string::npos);
  ASSERT_TRUE(normalized.find("autopoll=static_cast<constMessagePoll*>(content);") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(poll->attached_media!=nullptr){returntrue;}") != td::string::npos);
  ASSERT_TRUE(normalized.find("auto*poll_manager=get_poll_manager_for_content_access(td,poll->poll_id,"
                              "\"message_content_poll_has_media\");") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(poll_manager==nullptr){returntrue;}") != td::string::npos);
  ASSERT_TRUE(normalized.find("return!poll_manager->get_poll_file_ids(poll->poll_id).empty();") != td::string::npos);

  const auto attached_pos = normalized.find("if(poll->attached_media!=nullptr){returntrue;}");
  const auto helper_pos = normalized.find(
      "auto*poll_manager=get_poll_manager_for_content_access(td,poll->poll_id,"
      "\"message_content_poll_has_media\");");
  const auto fallback_pos = normalized.find("if(poll_manager==nullptr){returntrue;}");
  const auto lookup_pos = normalized.find("return!poll_manager->get_poll_file_ids(poll->poll_id).empty();");

  ASSERT_TRUE(attached_pos != td::string::npos);
  ASSERT_TRUE(helper_pos != td::string::npos);
  ASSERT_TRUE(fallback_pos != td::string::npos);
  ASSERT_TRUE(lookup_pos != td::string::npos);
  ASSERT_TRUE(attached_pos < helper_pos);
  ASSERT_TRUE(helper_pos < fallback_pos);
  ASSERT_TRUE(fallback_pos < lookup_pos);
}

TEST(QuickReplyPollMediaAdversarial, PollFileIdExtractionChecksStateBeforePollLookup) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");
  auto region =
      extract_region(source,
                     "vector<FileId> get_message_content_file_ids(const MessageContent *content, "
                     "const Td *td) {",
                     "StoryFullId get_message_content_story_full_id(const Td *td, const MessageContent *content)");
  auto normalized = normalize_for_contract(region);

  const auto guarded_lookup_pos = normalized.find(
      "auto*poll_manager=get_poll_manager_for_content_access(td,poll->poll_id,"
      "\"get_message_content_file_ids\");");
  const auto lookup_pos = normalized.find("poll_manager->get_poll_file_ids(poll->poll_id);");
  const auto old_direct_lookup_pos = normalized.find("autoresult=td->poll_manager_->get_poll_file_ids(poll->poll_id);");

  ASSERT_TRUE(guarded_lookup_pos != td::string::npos);
  ASSERT_TRUE(lookup_pos != td::string::npos);
  ASSERT_TRUE(guarded_lookup_pos < lookup_pos);
  ASSERT_TRUE(old_direct_lookup_pos == td::string::npos);
}

}  // namespace
