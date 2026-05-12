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

TEST(PollMediaSendGuardAdversarial, SendGateChecksPollMediaBeforeChannelPolicyBranches) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");
  auto send_region =
      extract_region(source, "Status can_send_message_content(DialogId dialog_id, const MessageContent *content",
                     "bool can_forward_message_content(const Td *td, const MessageContent *content, bool is_copy)");
  auto poll_case_region =
      extract_region(send_region, "case MessageContentType::Poll:", "case MessageContentType::Sticker:");
  auto normalized = normalize_for_contract(poll_case_region);

  const auto media_guard_pos = normalized.find(
      "if(message_content_poll_has_media(content,td)){returnStatus::Error(400,\"Pollswithmediacan'tbesentyet\");}");
  const auto channel_policy_pos = normalized.find("if(dialog_type==DialogType::Channel){");
  const auto private_policy_pos =
      normalized.find("if(dialog_type==DialogType::User&&!is_forward&&!td->auth_manager_->is_bot()&&");

  ASSERT_TRUE(media_guard_pos != td::string::npos);
  ASSERT_TRUE(channel_policy_pos != td::string::npos);
  ASSERT_TRUE(private_policy_pos != td::string::npos);
  ASSERT_TRUE(media_guard_pos < channel_policy_pos);
  ASSERT_TRUE(media_guard_pos < private_policy_pos);
}

TEST(PollMediaSendGuardAdversarial, ForwardCopyChecksMediaBeforePollManagerInputProbe) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");
  auto region = extract_region(
      source, "bool can_forward_message_content(const Td *td, const MessageContent *content, bool is_copy)",
      "bool update_opened_message_content(MessageContent *content)");
  auto normalized = normalize_for_contract(region);

  const auto media_guard_pos = normalized.find("if(message_content_poll_has_media(content,td)){returnfalse;}");
  const auto poll_manager_probe_pos =
      normalized.find("if(poll_manager==nullptr||!poll_manager->has_input_media(poll_id)){returnfalse;}");

  ASSERT_TRUE(media_guard_pos != td::string::npos);
  ASSERT_TRUE(poll_manager_probe_pos != td::string::npos);
  ASSERT_TRUE(media_guard_pos < poll_manager_probe_pos);
}

TEST(PollMediaSendGuardAdversarial, PollManagerStopsBeforeInputMediaConstructionWhenPollHasMedia) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = extract_region(
      source, "tl_object_ptr<telegram_api::InputMedia> PollManager::get_input_media(PollId poll_id) const {",
      "PollId PollManager::on_get_poll(PollId poll_id,");
  auto normalized = normalize_for_contract(region);

  const auto media_guard_pos = normalized.find("if(!get_poll_file_ids(poll_id).empty()){");
  const auto build_pos = normalized.find("returntelegram_api::make_object<telegram_api::inputMediaPoll>(");
  const auto fail_closed_log_pos = normalized.find(
      "LOG(ERROR)<<\"Fail-closedpollinputmediagenerationfor\"<<poll_id<<\":pollcontainsmediafields"
      "thataren'tsupportedininputMediaPoll\";");

  ASSERT_TRUE(media_guard_pos != td::string::npos);
  ASSERT_TRUE(build_pos != td::string::npos);
  ASSERT_TRUE(fail_closed_log_pos != td::string::npos);
  ASSERT_TRUE(media_guard_pos < build_pos);
  ASSERT_TRUE(fail_closed_log_pos < build_pos);
}

}  // namespace
