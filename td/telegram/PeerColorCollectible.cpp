//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PeerColorCollectible.h"

#include "td/utils/logging.h"

namespace td {

PeerColorCollectible::PeerColorCollectible(telegram_api::object_ptr<telegram_api::peerColorCollectible> peer_color) {
  CHECK(peer_color != nullptr);
  unique_gift_id_ = peer_color->collectible_id_;
  gift_custom_emoji_id_ = CustomEmojiId(peer_color->gift_emoji_id_);
  if (!gift_custom_emoji_id_.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(peer_color);
    gift_custom_emoji_id_ = {};
  }
  background_custom_emoji_id_ = CustomEmojiId(peer_color->background_emoji_id_);
  if (!background_custom_emoji_id_.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(peer_color);
    background_custom_emoji_id_ = {};
  }
  light_accent_color_ = peer_color->accent_color_;
  light_colors_ = peer_color->colors_;
  if (light_colors_.size() > 3u) {
    LOG(ERROR) << "Receive " << to_string(peer_color);
    light_colors_ = {};
  }
  if ((peer_color->flags_ & telegram_api::peerColorCollectible::DARK_ACCENT_COLOR_MASK) != 0) {
    dark_accent_color_ = peer_color->dark_accent_color_;
  } else {
    dark_accent_color_ = light_accent_color_;
  }
  if ((peer_color->flags_ & telegram_api::peerColorCollectible::DARK_COLORS_MASK) != 0) {
    dark_colors_ = peer_color->dark_colors_;
    if (dark_colors_.size() > 3u) {
      LOG(ERROR) << "Receive " << to_string(peer_color);
      dark_colors_ = {};
    }
  } else {
    dark_colors_ = light_colors_;
  }
}

unique_ptr<PeerColorCollectible> PeerColorCollectible::get_peer_color_collectible(
    telegram_api::object_ptr<telegram_api::peerColorCollectible> peer_color) {
  CHECK(peer_color != nullptr);
  auto result = make_unique<PeerColorCollectible>(std::move(peer_color));
  if (!result->is_valid()) {
    return nullptr;
  }
  return result;
}

td_api::object_ptr<td_api::upgradedGiftColors> PeerColorCollectible::get_upgraded_gift_colors_object() const {
  return td_api::make_object<td_api::upgradedGiftColors>(
      unique_gift_id_, gift_custom_emoji_id_.get(), background_custom_emoji_id_.get(), light_accent_color_,
      vector<int32>(light_colors_), dark_accent_color_, vector<int32>(dark_colors_));
}

bool operator==(const PeerColorCollectible &lhs, const PeerColorCollectible &rhs) {
  return lhs.unique_gift_id_ == rhs.unique_gift_id_ && lhs.gift_custom_emoji_id_ == rhs.gift_custom_emoji_id_ &&
         lhs.background_custom_emoji_id_ == rhs.background_custom_emoji_id_ &&
         lhs.light_accent_color_ == rhs.light_accent_color_ && lhs.light_colors_ == rhs.light_colors_ &&
         lhs.dark_accent_color_ == rhs.dark_accent_color_ && lhs.dark_colors_ == rhs.dark_colors_;
}

}  // namespace td
