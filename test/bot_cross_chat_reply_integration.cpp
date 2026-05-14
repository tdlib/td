// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/bot_cross_chat_reply_test_utils.h"

#include <vector>

TEST(BotCrossChatReplyIntegration, GuestAndBotMessageObjectPathsShareInternalCrossChatReplyDropShape) {
  auto guest = td::bot_cross_chat_reply_test::normalized_guest_message_object_region();
  auto message = td::bot_cross_chat_reply_test::normalized_message_object_region();

  const std::vector<td::Slice> guest_required = {
      R"(if(!m->replied_message_info.is_external()&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==MessageId()){returnnullptr;})",
      R"(returnm->replied_message_info.get_message_reply_to_message_object(td_,dialog_id,m->message_id);)",
  };
  const std::vector<td::Slice> message_required = {
      R"(if(is_bot&&!m->replied_message_info.is_external()&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==MessageId()){returnnullptr;})",
      R"(if(!is_bot&&m->is_topic_message&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==m->top_thread_message_id){returnnullptr;})",
      R"(returnm->replied_message_info.get_message_reply_to_message_object(td_,dialog_id,m->message_id);)",
  };

  for (auto snippet : guest_required) {
    ASSERT_TRUE(guest.find(snippet.str()) != td::string::npos);
  }
  for (auto snippet : message_required) {
    ASSERT_TRUE(message.find(snippet.str()) != td::string::npos);
  }
}

TEST(BotCrossChatReplyIntegration, CrossChatReplyDropGuardsExecuteBeforeReplySerializationInBothPaths) {
  auto guest = td::bot_cross_chat_reply_test::normalized_guest_message_object_region();
  auto message = td::bot_cross_chat_reply_test::normalized_message_object_region();

  auto guest_guard_pos = guest.find(
      R"(if(!m->replied_message_info.is_external()&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==MessageId()){returnnullptr;})");
  auto guest_serialize_pos =
      guest.find(R"(returnm->replied_message_info.get_message_reply_to_message_object(td_,dialog_id,m->message_id);)");
  auto message_guard_pos = message.find(
      R"(if(is_bot&&!m->replied_message_info.is_external()&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==MessageId()){returnnullptr;})");
  auto message_serialize_pos = message.find(
      R"(returnm->replied_message_info.get_message_reply_to_message_object(td_,dialog_id,m->message_id);)");

  ASSERT_NE(td::string::npos, guest_guard_pos);
  ASSERT_NE(td::string::npos, guest_serialize_pos);
  ASSERT_NE(td::string::npos, message_guard_pos);
  ASSERT_NE(td::string::npos, message_serialize_pos);
  ASSERT_TRUE(guest_guard_pos < guest_serialize_pos);
  ASSERT_TRUE(message_guard_pos < message_serialize_pos);
}