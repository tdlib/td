// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/bot_cross_chat_reply_test_utils.h"

#include <array>

namespace {

struct SnippetCase {
  bool use_message_region;
  td::string snippet;
  bool expected_present;
};

}  // namespace

TEST(BotCrossChatReplyLightFuzz, DeterministicSnippetMatrixPreservesBotAndGuestReplyDropInvariants) {
  const auto guest = td::bot_cross_chat_reply_test::normalized_guest_message_object_region();
  const auto message = td::bot_cross_chat_reply_test::normalized_message_object_region();

  const std::array<SnippetCase, 6> cases = {{
      {true,
       R"(if(is_bot&&!m->replied_message_info.is_external()&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==MessageId()){returnnullptr;})",
       true},
      {true,
       R"(if(!m->replied_message_info.is_empty()){if(!is_bot&&m->is_topic_message&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==m->top_thread_message_id){returnnullptr;}returnm->replied_message_info.get_message_reply_to_message_object(td_,dialog_id,m->message_id);}if(m->reply_to_story_full_id.is_valid()){)",
       false},
      {true,
       R"(if(!is_bot&&m->is_topic_message&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==m->top_thread_message_id){returnnullptr;})",
       true},
      {false,
       R"(if(!m->replied_message_info.is_external()&&m->replied_message_info.get_same_chat_reply_to_message_id(false)==MessageId()){returnnullptr;})",
       true},
      {false, R"(returnm->replied_message_info.get_message_reply_to_message_object(td_,dialog_id,m->message_id);)",
       true},
      {true, R"(returnm->replied_message_info.get_message_reply_to_message_object(td_,dialog_id,m->message_id);)",
       true},
  }};

  for (int iteration = 0; iteration < 10000; iteration++) {
    const auto &test_case = cases[static_cast<size_t>(td::Random::fast(0, static_cast<int>(cases.size()) - 1))];
    const auto &region = test_case.use_message_region ? message : guest;

    ASSERT_EQ(test_case.expected_present, region.find(test_case.snippet) != td::string::npos);
  }
}