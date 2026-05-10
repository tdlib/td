// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/MessageReplyHeader.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/tests.h"

#include <cstdint>

namespace {

td::telegram_api::object_ptr<td::telegram_api::messageFwdHeader> make_reply_origin() {
  auto reply_from = td::make_tl_object<td::telegram_api::messageFwdHeader>();
  reply_from->date_ = 1712345678;
  return reply_from;
}

td::telegram_api::object_ptr<td::telegram_api::Peer> make_peer_user(std::int64_t user_id) {
  return td::make_tl_object<td::telegram_api::peerUser>(user_id);
}

TEST(MessageReplyHeaderStress, PromotionAndRejectionRemainDeterministicUnderLoad) {
  constexpr std::int32_t kIterations = 250000;

  for (std::int32_t i = 1; i <= kIterations; ++i) {
    td::telegram_api::messageReplyHeader promotable;
    promotable.forum_topic_ = true;
    promotable.reply_to_top_id_ = 0;
    promotable.reply_to_msg_id_ = i;
    promotable.reply_to_peer_id_ = nullptr;
    promotable.reply_from_ = make_reply_origin();

    td::MessageReplyHeader::normalize_topic_reply_header(promotable);
    ASSERT_EQ(promotable.reply_to_top_id_, i);
    ASSERT_EQ(promotable.reply_to_msg_id_, 0);

    td::MessageReplyHeader::normalize_topic_reply_header(promotable);
    ASSERT_EQ(promotable.reply_to_top_id_, i);
    ASSERT_EQ(promotable.reply_to_msg_id_, 0);

    td::telegram_api::messageReplyHeader cross_chat;
    cross_chat.forum_topic_ = true;
    cross_chat.reply_to_top_id_ = 0;
    cross_chat.reply_to_msg_id_ = i;
    cross_chat.reply_to_peer_id_ = make_peer_user(i + 1000000);
    cross_chat.reply_from_ = make_reply_origin();

    td::MessageReplyHeader::normalize_topic_reply_header(cross_chat);
    ASSERT_EQ(cross_chat.reply_to_top_id_, 0);
    ASSERT_EQ(cross_chat.reply_to_msg_id_, i);
  }
}

}  // namespace
