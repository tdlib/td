// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/MessageReplyHeader.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/tests.h"

#include <cstdint>
#include <random>

namespace {

td::telegram_api::object_ptr<td::telegram_api::messageFwdHeader> make_reply_origin(std::int32_t date) {
  auto reply_from = td::make_tl_object<td::telegram_api::messageFwdHeader>();
  reply_from->date_ = date;
  return reply_from;
}

td::telegram_api::object_ptr<td::telegram_api::Peer> make_peer_user(std::int64_t user_id) {
  return td::make_tl_object<td::telegram_api::peerUser>(user_id);
}

TEST(MessageReplyHeaderLightFuzz, NormalizationOnlyTriggersForExactPromotionShape) {
  std::mt19937_64 rng(0xA09ADF0C63ULL);  // NOSONAR: deterministic seed keeps this light-fuzz test reproducible.
  std::uniform_int_distribution<int> id_dist(-1000, 1000000);
  std::bernoulli_distribution bit(0.5);

  constexpr size_t kIterations = 10000;
  for (size_t i = 0; i < kIterations; ++i) {
    td::telegram_api::messageReplyHeader header;
    header.forum_topic_ = bit(rng);
    header.reply_to_top_id_ = bit(rng) ? id_dist(rng) : 0;
    header.reply_to_msg_id_ = bit(rng) ? id_dist(rng) : 0;

    if (bit(rng)) {
      header.reply_from_ = make_reply_origin(1712345000 + static_cast<std::int32_t>(i));
    }
    if (bit(rng)) {
      header.reply_to_peer_id_ = make_peer_user(static_cast<std::int64_t>(1000 + i));
    }

    const auto old_top = header.reply_to_top_id_;
    const auto old_msg = header.reply_to_msg_id_;
    const auto old_forum = header.forum_topic_;
    const auto had_origin = header.reply_from_ != nullptr;
    const auto had_peer = header.reply_to_peer_id_ != nullptr;

    td::MessageReplyHeader::normalize_topic_reply_header(header);

    const bool changed = (header.reply_to_top_id_ != old_top) || (header.reply_to_msg_id_ != old_msg);
    if (changed) {
      ASSERT_EQ(old_top, 0);
      ASSERT_TRUE(old_forum);
      ASSERT_TRUE(had_origin);
      ASSERT_FALSE(had_peer);
      ASSERT_TRUE(old_msg > 0);
      ASSERT_EQ(header.reply_to_top_id_, old_msg);
      ASSERT_EQ(header.reply_to_msg_id_, 0);
    } else {
      ASSERT_EQ(header.reply_to_top_id_, old_top);
      ASSERT_EQ(header.reply_to_msg_id_, old_msg);
    }
  }
}

TEST(MessageReplyHeaderLightFuzz, FinalizationOnlyEmitsEarlierServerThreadAnchors) {
  std::mt19937_64 rng(0xB63A09ADF0ULL);  // NOSONAR: deterministic seed keeps this light-fuzz test reproducible.
  std::uniform_int_distribution<int> base_dist(5000, 1000000);
  std::uniform_int_distribution<int> offset_dist(1, 256);
  std::uniform_int_distribution<int> mode_dist(0, 4);

  auto make_candidate = [&](int mode, td::MessageId message_id) {
    const auto base = message_id.get_server_message_id().get();
    switch (mode) {
      case 0:
        return td::MessageId();
      case 1:
        return td::MessageId(td::ServerMessageId(base - offset_dist(rng)));
      case 2:
        return td::MessageId(td::ServerMessageId(base));
      case 3:
        return td::MessageId(td::ServerMessageId(base + offset_dist(rng)));
      case 4:
      default:
        return td::MessageId::min();
    }
  };

  auto is_acceptable_anchor = [](td::MessageId candidate, td::MessageId message_id) {
    return candidate.is_server() && candidate < message_id;
  };

  constexpr size_t kIterations = 10000;
  for (size_t i = 0; i < kIterations; ++i) {
    const auto message_id = td::MessageId(td::ServerMessageId(base_dist(rng)));
    auto top_thread_message_id = make_candidate(mode_dist(rng), message_id);
    const auto same_chat_reply_to_message_id = make_candidate(mode_dist(rng), message_id);
    const auto original_top_thread_message_id = top_thread_message_id;
    bool is_topic_message = true;

    const bool rejected_invalid_thread = td::MessageReplyHeader::finalize_channel_topic_thread_state(
        message_id, same_chat_reply_to_message_id, top_thread_message_id, is_topic_message);

    const bool explicit_anchor_acceptable = is_acceptable_anchor(original_top_thread_message_id, message_id);
    const bool fallback_anchor_acceptable = is_acceptable_anchor(same_chat_reply_to_message_id, message_id);
    if (explicit_anchor_acceptable) {
      ASSERT_FALSE(rejected_invalid_thread);
      ASSERT_EQ(top_thread_message_id, original_top_thread_message_id);
      ASSERT_TRUE(top_thread_message_id.is_server());
      ASSERT_TRUE(top_thread_message_id < message_id);
      ASSERT_TRUE(is_topic_message);
    } else if (fallback_anchor_acceptable) {
      ASSERT_FALSE(rejected_invalid_thread);
      ASSERT_EQ(top_thread_message_id, same_chat_reply_to_message_id);
      ASSERT_TRUE(top_thread_message_id.is_server());
      ASSERT_TRUE(top_thread_message_id < message_id);
      ASSERT_TRUE(is_topic_message);
    } else {
      ASSERT_EQ(rejected_invalid_thread,
                original_top_thread_message_id.is_valid() || same_chat_reply_to_message_id.is_valid());
      ASSERT_FALSE(top_thread_message_id.is_valid());
      ASSERT_FALSE(is_topic_message);
    }
  }
}

}  // namespace
