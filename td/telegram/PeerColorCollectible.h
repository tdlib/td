//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class PeerColorCollectible {
  int64 unique_gift_id_ = 0;
  CustomEmojiId gift_custom_emoji_id_;
  CustomEmojiId background_custom_emoji_id_;
  int32 light_accent_color_;
  vector<int32> light_colors_;
  int32 dark_accent_color_;
  vector<int32> dark_colors_;

  friend bool operator==(const PeerColorCollectible &lhs, const PeerColorCollectible &rhs);

 public:
  PeerColorCollectible() = default;

  explicit PeerColorCollectible(telegram_api::object_ptr<telegram_api::peerColorCollectible> peer_color);

  static unique_ptr<PeerColorCollectible> get_peer_color_collectible(
      telegram_api::object_ptr<telegram_api::peerColorCollectible> peer_color);

  bool is_valid() const {
    return !light_colors_.empty() && !dark_colors_.empty();
  }

  td_api::object_ptr<td_api::upgradedGiftColors> get_upgraded_gift_colors_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const PeerColorCollectible &lhs, const PeerColorCollectible &rhs);

inline bool operator!=(const PeerColorCollectible &lhs, const PeerColorCollectible &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
