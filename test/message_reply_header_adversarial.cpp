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
  return td::make_tl_object<td::telegram_api::peerUser>(777);
}

TEST(MessageReplyHeaderAdversarial, DoesNotPromoteCrossChatReplyIntoThreadAnchor) {
  auto reply_header = make_base_reply_header();
  reply_header.forum_topic_ = true;
  reply_header.reply_to_msg_id_ = 2001;
  reply_header.reply_to_peer_id_ = make_peer_user();
  reply_header.reply_from_ = make_reply_origin();

  td::MessageReplyHeader::normalize_topic_reply_header(reply_header);

  ASSERT_EQ(reply_header.reply_to_top_id_, 0);
  ASSERT_EQ(reply_header.reply_to_msg_id_, 2001);
}

TEST(MessageReplyHeaderAdversarial, DoesNotPromoteMissingOriginIntoThreadAnchor) {
  auto reply_header = make_base_reply_header();
  reply_header.forum_topic_ = true;
  reply_header.reply_to_msg_id_ = 2002;
  reply_header.reply_to_peer_id_ = nullptr;
  reply_header.reply_from_ = nullptr;

  td::MessageReplyHeader::normalize_topic_reply_header(reply_header);

  ASSERT_EQ(reply_header.reply_to_top_id_, 0);
  ASSERT_EQ(reply_header.reply_to_msg_id_, 2002);
}

TEST(MessageReplyHeaderAdversarial, DoesNotPromoteWithoutReplyToMessageId) {
  auto reply_header = make_base_reply_header();
  reply_header.forum_topic_ = true;
  reply_header.reply_to_top_id_ = 0;
  reply_header.reply_to_msg_id_ = 0;
  reply_header.reply_to_peer_id_ = nullptr;
  reply_header.reply_from_ = make_reply_origin();

  td::MessageReplyHeader::normalize_topic_reply_header(reply_header);

  ASSERT_EQ(reply_header.reply_to_top_id_, 0);
  ASSERT_EQ(reply_header.reply_to_msg_id_, 0);
}

TEST(MessageReplyHeaderAdversarial, KeepsConflictingTopIdAndReplyMessageIdAsIs) {
  auto reply_header = make_base_reply_header();
  reply_header.forum_topic_ = true;
  reply_header.reply_to_top_id_ = 9000;
  reply_header.reply_to_msg_id_ = 1337;
  reply_header.reply_from_ = make_reply_origin();

  td::MessageReplyHeader::normalize_topic_reply_header(reply_header);

  ASSERT_EQ(reply_header.reply_to_top_id_, 9000);
  ASSERT_EQ(reply_header.reply_to_msg_id_, 1337);
}

TEST(MessageReplyHeaderAdversarial, DoesNotPromoteNonPositiveReplyMessageIdIntoThreadAnchor) {
  auto reply_header = make_base_reply_header();
  reply_header.forum_topic_ = true;
  reply_header.reply_to_top_id_ = 0;
  reply_header.reply_to_msg_id_ = -1;
  reply_header.reply_to_peer_id_ = nullptr;
  reply_header.reply_from_ = make_reply_origin();

  td::MessageReplyHeader::normalize_topic_reply_header(reply_header);

  ASSERT_EQ(reply_header.reply_to_top_id_, 0);
  ASSERT_EQ(reply_header.reply_to_msg_id_, -1);
}

TEST(MessageReplyHeaderAdversarial, ClearsTopicFlagWhenFallbackThreadTargetIsNotEarlierThanMessage) {
  auto top_thread_message_id = td::MessageId();
  const auto same_chat_reply_to_message_id = td::MessageId(td::ServerMessageId(5000));
  bool is_topic_message = true;

  const bool rejected_invalid_thread = td::MessageReplyHeader::finalize_channel_topic_thread_state(
      td::MessageId(td::ServerMessageId(5000)), same_chat_reply_to_message_id, top_thread_message_id, is_topic_message);

  ASSERT_TRUE(rejected_invalid_thread);
  ASSERT_FALSE(top_thread_message_id.is_valid());
  ASSERT_FALSE(is_topic_message);
}

TEST(MessageReplyHeaderAdversarial, ClearsTopicFlagWhenExplicitAndFallbackThreadTargetsAreNotEarlierThanMessage) {
  auto top_thread_message_id = td::MessageId(td::ServerMessageId(7000));
  const auto same_chat_reply_to_message_id = td::MessageId(td::ServerMessageId(5000));
  bool is_topic_message = true;

  const bool rejected_invalid_thread = td::MessageReplyHeader::finalize_channel_topic_thread_state(
      td::MessageId(td::ServerMessageId(5000)), same_chat_reply_to_message_id, top_thread_message_id, is_topic_message);

  ASSERT_TRUE(rejected_invalid_thread);
  ASSERT_FALSE(top_thread_message_id.is_valid());
  ASSERT_FALSE(is_topic_message);
}

TEST(MessageReplyHeaderAdversarial, ClearsTopicFlagWhenOnlyNonServerExplicitThreadAnchorRemains) {
  auto top_thread_message_id = td::MessageId::min();
  const auto same_chat_reply_to_message_id = td::MessageId();
  bool is_topic_message = true;

  const bool rejected_invalid_thread = td::MessageReplyHeader::finalize_channel_topic_thread_state(
      td::MessageId(td::ServerMessageId(5000)), same_chat_reply_to_message_id, top_thread_message_id, is_topic_message);

  ASSERT_TRUE(rejected_invalid_thread);
  ASSERT_FALSE(top_thread_message_id.is_valid());
  ASSERT_FALSE(is_topic_message);
}

TEST(MessageReplyHeaderAdversarial, RejectsMalformedRawExplicitThreadAnchorEncoding) {
  auto top_thread_message_id = td::MessageId(static_cast<td::int64>(-1));
  const auto same_chat_reply_to_message_id = td::MessageId();
  bool is_topic_message = true;

  const bool rejected_invalid_thread = td::MessageReplyHeader::finalize_channel_topic_thread_state(
      td::MessageId(td::ServerMessageId(5000)), same_chat_reply_to_message_id, top_thread_message_id, is_topic_message);

  ASSERT_TRUE(rejected_invalid_thread);
  ASSERT_FALSE(top_thread_message_id.is_valid());
  ASSERT_FALSE(is_topic_message);
}

TEST(MessageReplyHeaderAdversarial, RejectsMalformedRawFallbackThreadAnchorEncoding) {
  auto top_thread_message_id = td::MessageId();
  const auto same_chat_reply_to_message_id = td::MessageId(static_cast<td::int64>(-1));
  bool is_topic_message = true;

  const bool rejected_invalid_thread = td::MessageReplyHeader::finalize_channel_topic_thread_state(
      td::MessageId(td::ServerMessageId(5000)), same_chat_reply_to_message_id, top_thread_message_id, is_topic_message);

  ASSERT_TRUE(rejected_invalid_thread);
  ASSERT_FALSE(top_thread_message_id.is_valid());
  ASSERT_FALSE(is_topic_message);
}

}  // namespace
