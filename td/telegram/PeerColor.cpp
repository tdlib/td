//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PeerColor.h"

#include "td/utils/logging.h"

namespace td {

PeerColor::PeerColor(const telegram_api::object_ptr<telegram_api::PeerColor> &peer_color_ptr) {
  if (peer_color_ptr == nullptr) {
    return;
  }
  if (peer_color_ptr->get_id() != telegram_api::peerColor::ID) {
    LOG(ERROR) << "Receive " << to_string(peer_color_ptr);
    return;
  }
  auto peer_color = static_cast<const telegram_api::peerColor *>(peer_color_ptr.get());
  if ((peer_color->flags_ & telegram_api::peerColor::COLOR_MASK) != 0) {
    accent_color_id_ = AccentColorId(peer_color->color_);
    if (!accent_color_id_.is_valid()) {
      LOG(ERROR) << "Receive " << to_string(peer_color_ptr);
      accent_color_id_ = AccentColorId();
    }
  }
  if (peer_color->background_emoji_id_ != 0) {
    background_custom_emoji_id_ = CustomEmojiId(peer_color->background_emoji_id_);
    if (!background_custom_emoji_id_.is_valid()) {
      LOG(ERROR) << "Receive " << to_string(peer_color_ptr);
      background_custom_emoji_id_ = CustomEmojiId();
    }
  }
}

}  // namespace td
