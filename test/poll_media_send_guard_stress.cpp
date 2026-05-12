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

TEST(PollMediaSendGuardStress, RepeatedSourceReadsKeepFailClosedPollMediaGuardsStable) {
  constexpr int kIterations = 2200;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; ++i) {
    auto message_content_source =
        normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp"));
    auto poll_manager_source =
        normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp"));

    ASSERT_NE(td::string::npos,
              message_content_source.find("if(message_content_poll_has_media(content,td)){returnStatus::Error(400,"
                                          "\"Pollswithmediacan'tbesentyet\");}"));
    ASSERT_NE(td::string::npos,
              message_content_source.find("if(message_content_poll_has_media(content,td)){returnfalse;}"));

    ASSERT_NE(td::string::npos, poll_manager_source.find("boolPollManager::has_input_media(PollIdpoll_id)const{"));
    ASSERT_NE(td::string::npos, poll_manager_source.find("if(!get_poll_file_ids(poll_id).empty()){returnfalse;}"));
    ASSERT_NE(td::string::npos,
              poll_manager_source.find(
                  "tl_object_ptr<telegram_api::InputMedia>PollManager::get_input_media(PollIdpoll_id)const{"));
    ASSERT_NE(td::string::npos, poll_manager_source.find("if(!get_poll_file_ids(poll_id).empty()){"));
    ASSERT_NE(td::string::npos, poll_manager_source.find("returnnullptr;"));
    ASSERT_NE(td::string::npos,
              poll_manager_source.find("returntelegram_api::make_object<telegram_api::inputMediaPoll>("));

    const auto send_media_guard_pos = message_content_source.find(
        "if(message_content_poll_has_media(content,td)){returnStatus::Error(400,\"Pollswithmediacan'tbesentyet\");}");
    const auto forward_media_guard_pos =
        message_content_source.find("if(message_content_poll_has_media(content,td)){returnfalse;}");
    const auto has_input_media_pos = poll_manager_source.find("boolPollManager::has_input_media(PollIdpoll_id)const{");
    const auto poll_file_gate_pos = poll_manager_source.find("if(!get_poll_file_ids(poll_id).empty()){returnfalse;}");
    const auto get_input_media_pos = poll_manager_source.find(
        "tl_object_ptr<telegram_api::InputMedia>PollManager::get_input_media(PollIdpoll_id)const{");
    const auto get_input_media_gate_pos = poll_manager_source.find("if(!get_poll_file_ids(poll_id).empty()){");
    const auto build_input_media_pos =
        poll_manager_source.find("returntelegram_api::make_object<telegram_api::inputMediaPoll>(");

    ASSERT_TRUE(send_media_guard_pos != td::string::npos);
    ASSERT_TRUE(forward_media_guard_pos != td::string::npos);
    ASSERT_TRUE(has_input_media_pos != td::string::npos);
    ASSERT_TRUE(poll_file_gate_pos != td::string::npos);
    ASSERT_TRUE(get_input_media_pos != td::string::npos);
    ASSERT_TRUE(get_input_media_gate_pos != td::string::npos);
    ASSERT_TRUE(build_input_media_pos != td::string::npos);
    ASSERT_TRUE(has_input_media_pos < poll_file_gate_pos);
    ASSERT_TRUE(get_input_media_pos < build_input_media_pos);
    ASSERT_TRUE(get_input_media_gate_pos < build_input_media_pos);

    checksum += static_cast<td::uint32>(message_content_source.size() + poll_manager_source.size() +
                                        static_cast<size_t>(i) + send_media_guard_pos + forward_media_guard_pos +
                                        has_input_media_pos + poll_file_gate_pos + get_input_media_pos +
                                        get_input_media_gate_pos + build_input_media_pos);
  }

  ASSERT_TRUE(checksum != 0);
}
