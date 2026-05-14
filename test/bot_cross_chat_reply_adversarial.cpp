// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/bot_cross_chat_reply_test_utils.h"

TEST(BotCrossChatReplyAdversarial, LegacyBotMessageObjectFlowMustNotBypassCrossChatDropGuard) {
  auto normalized = td::bot_cross_chat_reply_test::normalized_message_object_region();

  ASSERT_TRUE(
      normalized.find(
          R"(if(!m->replied_message_info.is_empty()){if(!is_bot&&m->is_topic_message&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==m->top_thread_message_id){returnnullptr;}returnm->replied_message_info.get_message_reply_to_message_object(td_,dialog_id,m->message_id);}if(m->reply_to_story_full_id.is_valid()){)") ==
      td::string::npos);
}

TEST(BotCrossChatReplyAdversarial, NonBotMessageObjectPathMustNotDropAllInternalCrossChatReplies) {
  auto normalized = td::bot_cross_chat_reply_test::normalized_message_object_region();

  ASSERT_TRUE(
      normalized.find(
          R"(if(!m->replied_message_info.is_external()&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==MessageId()){returnnullptr;})") ==
      td::string::npos);
}

TEST(BotCrossChatReplyAdversarial, GuestMessageObjectPathMustNotSerializeInternalCrossChatRepliesUnguarded) {
  auto normalized = td::bot_cross_chat_reply_test::normalized_guest_message_object_region();

  auto guard_pos = normalized.find(
      R"(if(!m->replied_message_info.is_external()&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==MessageId()){returnnullptr;})");
  auto serialize_pos = normalized.find(
      R"(returnm->replied_message_info.get_message_reply_to_message_object(td_,dialog_id,m->message_id);)");

  ASSERT_NE(td::string::npos, guard_pos);
  ASSERT_NE(td::string::npos, serialize_pos);
  ASSERT_TRUE(guard_pos < serialize_pos);
}