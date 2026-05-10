// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/ChannelId.h"
#include "td/telegram/MessageReplyHeader.h"
#include "td/telegram/RepliedMessageInfo.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/tests.h"

namespace {

td::telegram_api::object_ptr<td::telegram_api::messageFwdHeader> make_reply_origin() {
  auto reply_from = td::make_tl_object<td::telegram_api::messageFwdHeader>();
  reply_from->date_ = 1712345678;
  return reply_from;
}

td::telegram_api::object_ptr<td::telegram_api::messageReplyHeader> make_promotable_topic_reply_header(
    td::int32 reply_to_message_id) {
  auto reply_header = td::make_tl_object<td::telegram_api::messageReplyHeader>();
  reply_header->forum_topic_ = true;
  reply_header->reply_to_top_id_ = 0;
  reply_header->reply_to_msg_id_ = reply_to_message_id;
  reply_header->reply_to_peer_id_ = nullptr;
  reply_header->reply_from_ = nullptr;
  return reply_header;
}

TEST(MessageReplyHeaderIntegration, UnnormalizedHeaderLeavesSameChatReplyInParsedInfo) {
  auto reply_header = make_promotable_topic_reply_header(4242);

  auto info =
      td::RepliedMessageInfo(nullptr, std::move(reply_header), td::DialogId(td::ChannelId(static_cast<td::int64>(1))),
                             td::MessageId(td::ServerMessageId(5000)), 1712345678);

  ASSERT_EQ(info.get_same_chat_reply_to_message_id(false), td::MessageId(td::ServerMessageId(4242)));
}

TEST(MessageReplyHeaderIntegration, NormalizedHeaderConsumesSameChatReplyDuringParsing) {
  auto reply_header = make_promotable_topic_reply_header(4242);
  reply_header->reply_from_ = make_reply_origin();
  td::MessageReplyHeader::normalize_topic_reply_header(*reply_header);

  ASSERT_EQ(reply_header->reply_to_top_id_, 4242);
  ASSERT_EQ(reply_header->reply_to_msg_id_, 0);
  reply_header->reply_from_ = nullptr;

  auto info =
      td::RepliedMessageInfo(nullptr, std::move(reply_header), td::DialogId(td::ChannelId(static_cast<td::int64>(1))),
                             td::MessageId(td::ServerMessageId(5000)), 1712345678);

  ASSERT_FALSE(info.get_same_chat_reply_to_message_id(false).is_valid());
}

TEST(MessageReplyHeaderIntegration, ParsedSameChatFallbackRepairsNonServerExplicitThreadAnchor) {
  auto reply_header = make_promotable_topic_reply_header(4242);

  auto info =
      td::RepliedMessageInfo(nullptr, std::move(reply_header), td::DialogId(td::ChannelId(static_cast<td::int64>(1))),
                             td::MessageId(td::ServerMessageId(5000)), 1712345678);
  const auto same_chat_reply_to_message_id = info.get_same_chat_reply_to_message_id(false);

  auto top_thread_message_id = td::MessageId::min();
  bool is_topic_message = true;

  const bool rejected_invalid_thread = td::MessageReplyHeader::finalize_channel_topic_thread_state(
      td::MessageId(td::ServerMessageId(5000)), same_chat_reply_to_message_id, top_thread_message_id, is_topic_message);

  ASSERT_FALSE(rejected_invalid_thread);
  ASSERT_EQ(top_thread_message_id, same_chat_reply_to_message_id);
  ASSERT_TRUE(top_thread_message_id.is_server());
  ASSERT_TRUE(is_topic_message);
}

}  // namespace
