// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    switch (static_cast<unsigned char>(c)) {
      case ' ':
      case '\t':
      case '\r':
      case '\n':
        continue;
      default:
        break;
    }
    normalized.push_back(c);
  }
  return normalized;
}

}  // namespace

TEST(QuickReplyPollMediaStress, RepeatedSourceReadsKeepAnyMediaGuardStable) {
  constexpr int kIterations = 2200;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; ++i) {
    auto quick_reply_source =
        normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/QuickReplyManager.cpp"));
    auto message_content_source =
        normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp"));

    ASSERT_NE(td::string::npos, quick_reply_source.find("if(message_content_poll_has_media(content,td_)){"));
    ASSERT_NE(td::string::npos,
              quick_reply_source.find("if(message_content_poll_has_media(message->content.get(),td_)){"));
    ASSERT_NE(td::string::npos, quick_reply_source.find("Can'tsendpollswithmediafromquickreplies"));
    ASSERT_EQ(td::string::npos, quick_reply_source.find("message_content_poll_has_attached_media(content)"));
    ASSERT_EQ(td::string::npos,
              quick_reply_source.find("message_content_poll_has_attached_media(message->content.get())"));

    ASSERT_NE(td::string::npos, message_content_source.find(
                                    "boolmessage_content_poll_has_media(constMessageContent*content,constTd*td){"));
    ASSERT_NE(td::string::npos, message_content_source.find("if(content==nullptr){returnfalse;}"));
    ASSERT_NE(td::string::npos,
              message_content_source.find("if(content->get_type()!=MessageContentType::Poll){returnfalse;}"));
    ASSERT_NE(td::string::npos, message_content_source.find("if(td==nullptr){"));
    ASSERT_NE(td::string::npos, message_content_source.find("if(poll->attached_media!=nullptr){returntrue;}"));
    ASSERT_NE(td::string::npos,
              message_content_source.find("auto*poll_manager=get_poll_manager_for_content_access(td,poll->poll_id,"
                                          "\"message_content_poll_has_media\");"));
    ASSERT_NE(td::string::npos, message_content_source.find("if(poll_manager==nullptr){returntrue;}"));
    ASSERT_NE(td::string::npos,
              message_content_source.find("return!poll_manager->get_poll_file_ids(poll->poll_id).empty();"));
    ASSERT_EQ(td::string::npos,
              message_content_source.find("boolmessage_content_poll_has_attached_media(constMessageContent*content){"));
    ASSERT_EQ(td::string::npos,
              message_content_source.find("returnstatic_cast<constMessagePoll*>(content)->attached_media!=nullptr;"));

    auto do_send_guard_pos = quick_reply_source.find("if(message_content_poll_has_media(content,td_)){");
    auto export_guard_pos = quick_reply_source.find("if(message_content_poll_has_media(message->content.get(),td_)){");
    auto old_guard_pos = quick_reply_source.find("message_content_poll_has_attached_media(");
    auto attached_media_pos = message_content_source.find("if(poll->attached_media!=nullptr){returntrue;}");
    auto helper_pos = message_content_source.find(
        "auto*poll_manager=get_poll_manager_for_content_access(td,poll->poll_id,\"message_content_poll_has_media\");");
    auto fallback_pos = message_content_source.find("if(poll_manager==nullptr){returntrue;}");
    auto poll_state_pos = message_content_source.find("return!poll_manager->get_poll_file_ids(poll->poll_id).empty();");

    ASSERT_TRUE(do_send_guard_pos != td::string::npos);
    ASSERT_TRUE(export_guard_pos != td::string::npos);
    ASSERT_TRUE(old_guard_pos == td::string::npos);
    ASSERT_TRUE(attached_media_pos != td::string::npos);
    ASSERT_TRUE(helper_pos != td::string::npos);
    ASSERT_TRUE(fallback_pos != td::string::npos);
    ASSERT_TRUE(poll_state_pos != td::string::npos);
    ASSERT_TRUE(attached_media_pos < helper_pos);
    ASSERT_TRUE(helper_pos < fallback_pos);
    ASSERT_TRUE(fallback_pos < poll_state_pos);
    ASSERT_TRUE(attached_media_pos < poll_state_pos);

    checksum += static_cast<td::uint32>(quick_reply_source.size() + message_content_source.size() +
                                        static_cast<size_t>(i) + do_send_guard_pos + export_guard_pos +
                                        attached_media_pos + helper_pos + fallback_pos + poll_state_pos);
  }

  ASSERT_TRUE(checksum != 0);
}
