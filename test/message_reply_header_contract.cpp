// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/MessageReplyHeader.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/tests.h"

namespace {

td::telegram_api::messageReplyHeader make_base_reply_header() {
  return td::telegram_api::messageReplyHeader();
}

td::telegram_api::object_ptr<td::telegram_api::messageFwdHeader> make_reply_origin() {
  auto reply_from = td::make_tl_object<td::telegram_api::messageFwdHeader>();
  reply_from->date_ = 1712345678;
  return reply_from;
}

td::telegram_api::object_ptr<td::telegram_api::Peer> make_peer_user() {
  return td::make_tl_object<td::telegram_api::peerUser>(123456789);
}

TEST(MessageReplyHeaderContract, PreservesExplicitTopThreadIdForForumTopic) {
  auto reply_header = make_base_reply_header();
  reply_header.forum_topic_ = true;
  reply_header.reply_to_top_id_ = 3100;
  reply_header.reply_to_msg_id_ = 2200;
  reply_header.reply_from_ = make_reply_origin();

  td::MessageReplyHeader::normalize_topic_reply_header(reply_header);

  ASSERT_EQ(reply_header.reply_to_top_id_, 3100);
  ASSERT_EQ(reply_header.reply_to_msg_id_, 2200);
}

TEST(MessageReplyHeaderContract, NormalizesMissingTopIdIntoThreadAnchorWhenOriginPresent) {
  auto reply_header = make_base_reply_header();
  reply_header.forum_topic_ = true;
  reply_header.reply_to_top_id_ = 0;
  reply_header.reply_to_msg_id_ = 4242;
  reply_header.reply_to_peer_id_ = nullptr;
  reply_header.reply_from_ = make_reply_origin();

  td::MessageReplyHeader::normalize_topic_reply_header(reply_header);

  ASSERT_EQ(reply_header.reply_to_top_id_, 4242);
  ASSERT_EQ(reply_header.reply_to_msg_id_, 0);
}

TEST(MessageReplyHeaderContract, LeavesSameChatReplyIntactWhenNotForumTopic) {
  auto reply_header = make_base_reply_header();
  reply_header.forum_topic_ = false;
  reply_header.reply_to_top_id_ = 0;
  reply_header.reply_to_msg_id_ = 777;
  reply_header.reply_from_ = make_reply_origin();

  td::MessageReplyHeader::normalize_topic_reply_header(reply_header);

  ASSERT_EQ(reply_header.reply_to_top_id_, 0);
  ASSERT_EQ(reply_header.reply_to_msg_id_, 777);
}

TEST(MessageReplyHeaderContract, DoesNotRewriteWhenReplyToPeerIdIsPresent) {
  auto reply_header = make_base_reply_header();
  reply_header.forum_topic_ = true;
  reply_header.reply_to_top_id_ = 0;
  reply_header.reply_to_msg_id_ = 909;
  reply_header.reply_to_peer_id_ = make_peer_user();
  reply_header.reply_from_ = make_reply_origin();

  td::MessageReplyHeader::normalize_topic_reply_header(reply_header);

  ASSERT_EQ(reply_header.reply_to_top_id_, 0);
  ASSERT_EQ(reply_header.reply_to_msg_id_, 909);
}

TEST(MessageReplyHeaderContract, PromotesEarlierSameChatReplyFallbackIntoThreadAnchor) {
  auto top_thread_message_id = td::MessageId();
  const auto same_chat_reply_to_message_id = td::MessageId(td::ServerMessageId(4242));
  bool is_topic_message = true;

  const bool rejected_invalid_thread = td::MessageReplyHeader::finalize_channel_topic_thread_state(
      td::MessageId(td::ServerMessageId(5000)), same_chat_reply_to_message_id, top_thread_message_id, is_topic_message);

  ASSERT_FALSE(rejected_invalid_thread);
  ASSERT_EQ(top_thread_message_id, same_chat_reply_to_message_id);
  ASSERT_TRUE(is_topic_message);
}

TEST(MessageReplyHeaderContract, RepairsInvalidExplicitThreadAnchorUsingEarlierSameChatFallback) {
  auto top_thread_message_id = td::MessageId(td::ServerMessageId(7000));
  const auto same_chat_reply_to_message_id = td::MessageId(td::ServerMessageId(4242));
  bool is_topic_message = true;

  const bool rejected_invalid_thread = td::MessageReplyHeader::finalize_channel_topic_thread_state(
      td::MessageId(td::ServerMessageId(5000)), same_chat_reply_to_message_id, top_thread_message_id, is_topic_message);

  ASSERT_FALSE(rejected_invalid_thread);
  ASSERT_EQ(top_thread_message_id, same_chat_reply_to_message_id);
  ASSERT_TRUE(is_topic_message);
}

TEST(MessageReplyHeaderContract, RepairsNonServerExplicitThreadAnchorUsingEarlierServerFallback) {
  auto top_thread_message_id = td::MessageId::min();
  const auto same_chat_reply_to_message_id = td::MessageId(td::ServerMessageId(4242));
  bool is_topic_message = true;

  const bool rejected_invalid_thread = td::MessageReplyHeader::finalize_channel_topic_thread_state(
      td::MessageId(td::ServerMessageId(5000)), same_chat_reply_to_message_id, top_thread_message_id, is_topic_message);

  ASSERT_FALSE(rejected_invalid_thread);
  ASSERT_EQ(top_thread_message_id, same_chat_reply_to_message_id);
  ASSERT_TRUE(is_topic_message);
}

TEST(MessageReplyHeaderContract, KeepsEarlierExplicitThreadAnchorWhenFallbackAlsoExists) {
  const auto explicit_top_thread_message_id = td::MessageId(td::ServerMessageId(4100));
  auto top_thread_message_id = explicit_top_thread_message_id;
  const auto same_chat_reply_to_message_id = td::MessageId(td::ServerMessageId(4000));
  bool is_topic_message = true;

  const bool rejected_invalid_thread = td::MessageReplyHeader::finalize_channel_topic_thread_state(
      td::MessageId(td::ServerMessageId(5000)), same_chat_reply_to_message_id, top_thread_message_id, is_topic_message);

  ASSERT_FALSE(rejected_invalid_thread);
  ASSERT_EQ(top_thread_message_id, explicit_top_thread_message_id);
  ASSERT_TRUE(is_topic_message);
}

}  // namespace
