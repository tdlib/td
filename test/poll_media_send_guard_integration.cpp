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

TEST(PollMediaSendGuardIntegration, SendAndForwardPathsUseSamePollMediaHelper) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");

  auto can_send_region =
      extract_region(source, "Status can_send_message_content(DialogId dialog_id, const MessageContent *content",
                     "bool can_forward_message_content(const Td *td, const MessageContent *content, bool is_copy)");
  auto can_forward_region = extract_region(
      source, "bool can_forward_message_content(const Td *td, const MessageContent *content, bool is_copy)",
      "bool update_opened_message_content(MessageContent *content)");

  auto normalized_send = normalize_for_contract(can_send_region);
  auto normalized_forward = normalize_for_contract(can_forward_region);

  ASSERT_TRUE(normalized_send.find("message_content_poll_has_media(content,td)") != td::string::npos);
  ASSERT_TRUE(normalized_forward.find("message_content_poll_has_media(content,td)") != td::string::npos);
}

TEST(PollMediaSendGuardIntegration, PollManagerHasInputMediaAndGetInputMediaShareFailClosedMediaGate) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");

  auto has_input_media_region =
      extract_region(source, "bool PollManager::has_input_media(PollId poll_id) const {",
                     "tl_object_ptr<telegram_api::InputMedia> PollManager::get_input_media(PollId poll_id) const {");
  auto get_input_media_region = extract_region(
      source, "tl_object_ptr<telegram_api::InputMedia> PollManager::get_input_media(PollId poll_id) const {",
      "PollId PollManager::on_get_poll(PollId poll_id,");

  auto normalized_has_input_media = normalize_for_contract(has_input_media_region);
  auto normalized_get_input_media = normalize_for_contract(get_input_media_region);

  ASSERT_TRUE(normalized_has_input_media.find("if(!get_poll_file_ids(poll_id).empty()){returnfalse;}") !=
              td::string::npos);
  ASSERT_TRUE(normalized_get_input_media.find("if(!get_poll_file_ids(poll_id).empty()){") != td::string::npos);
  ASSERT_TRUE(normalized_get_input_media.find("returnnullptr;") != td::string::npos);
}

}  // namespace
