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

TEST(MessageReplyHeaderSourceContract, FinalizationUsesNamedThreadAnchorPredicates) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageReplyHeader.cpp");
  auto region =
      extract_region(source, "bool MessageReplyHeader::finalize_channel_topic_thread_state(",
                     "MessageReplyHeader::MessageReplyHeader(Td *td, tl_object_ptr<telegram_api::MessageReplyHeader>");

  ASSERT_TRUE(region.find("is_usable_channel_topic_thread_anchor(") != td::string::npos);
  ASSERT_TRUE(region.find("has_non_empty_thread_anchor(") != td::string::npos);
}

TEST(MessageReplyHeaderSourceContract, ConstructorFinalizationRemainsRoutedThroughDedicatedSeam) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageReplyHeader.cpp");
  auto region = extract_region(
      source, "MessageReplyHeader::MessageReplyHeader(Td *td, tl_object_ptr<telegram_api::MessageReplyHeader>",
      "}  // namespace td");

  ASSERT_TRUE(region.find("finalize_channel_topic_thread_state(") != td::string::npos);
  ASSERT_TRUE(
      region.find("same_chat_reply_to_message_id = replied_message_info_.get_same_chat_reply_to_message_id(false);") !=
      td::string::npos);
}

}  // namespace
