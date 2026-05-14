// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/bot_cross_chat_reply_test_utils.h"

TEST(BotCrossChatReplyContract, BotMessageObjectPathDropsInternalCrossChatRepliesBeforeSerialization) {
  auto normalized = td::bot_cross_chat_reply_test::normalized_message_object_region();

  ASSERT_TRUE(
      normalized.find(
          R"(if(is_bot&&!m->replied_message_info.is_external()&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==MessageId()){returnnullptr;})") !=
      td::string::npos);
}

TEST(BotCrossChatReplyContract, GuestMessageObjectPathKeepsInternalCrossChatReplyDropGuard) {
  auto normalized = td::bot_cross_chat_reply_test::normalized_guest_message_object_region();

  ASSERT_TRUE(
      normalized.find(
          R"(if(!m->replied_message_info.is_external()&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==MessageId()){returnnullptr;})") !=
      td::string::npos);
}

TEST(BotCrossChatReplyContract, BotGuardRunsBeforeTopicSuppressionAndReplySerialization) {
  auto normalized = td::bot_cross_chat_reply_test::normalized_message_object_region();

  auto bot_guard_pos = normalized.find(
      R"(if(is_bot&&!m->replied_message_info.is_external()&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==MessageId()){returnnullptr;})");
  auto topic_guard_pos = normalized.find(
      R"(if(!is_bot&&m->is_topic_message&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==m->top_thread_message_id){returnnullptr;})");
  auto serialize_pos = normalized.find(
      R"(returnm->replied_message_info.get_message_reply_to_message_object(td_,dialog_id,m->message_id);)");

  ASSERT_NE(td::string::npos, bot_guard_pos);
  ASSERT_NE(td::string::npos, topic_guard_pos);
  ASSERT_NE(td::string::npos, serialize_pos);
  ASSERT_TRUE(bot_guard_pos < topic_guard_pos);
  ASSERT_TRUE(topic_guard_pos < serialize_pos);
}