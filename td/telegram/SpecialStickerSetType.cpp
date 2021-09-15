//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SpecialStickerSetType.h"

#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"

namespace td {

string SpecialStickerSetType::animated_emoji() {
  return "animated_emoji_sticker_set";
}

string SpecialStickerSetType::animated_emoji_click() {
  return "animated_emoji_click_sticker_set";
}

string SpecialStickerSetType::animated_dice(const string &emoji) {
  CHECK(!emoji.empty());
  return PSTRING() << "animated_dice_sticker_set#" << emoji;
}

SpecialStickerSetType::SpecialStickerSetType(
    const telegram_api::object_ptr<telegram_api::InputStickerSet> &input_sticker_set) {
  CHECK(input_sticker_set != nullptr);
  switch (input_sticker_set->get_id()) {
    case telegram_api::inputStickerSetAnimatedEmoji::ID:
      type_ = animated_emoji();
      break;
    case telegram_api::inputStickerSetAnimatedEmojiAnimations::ID:
      type_ = animated_emoji_click();
      break;
    case telegram_api::inputStickerSetDice::ID:
      type_ = animated_dice(static_cast<const telegram_api::inputStickerSetDice *>(input_sticker_set.get())->emoticon_);
      break;
    default:
      UNREACHABLE();
      break;
  }
}

string SpecialStickerSetType::get_dice_emoji() const {
  if (begins_with(type_, "animated_dice_sticker_set#")) {
    return type_.substr(Slice("animated_dice_sticker_set#").size());
  }
  return string();
}

telegram_api::object_ptr<telegram_api::InputStickerSet> SpecialStickerSetType::get_input_sticker_set() const {
  if (type_ == animated_emoji()) {
    return telegram_api::make_object<telegram_api::inputStickerSetAnimatedEmoji>();
  }
  if (type_ == animated_emoji_click()) {
    return telegram_api::make_object<telegram_api::inputStickerSetAnimatedEmojiAnimations>();
  }
  auto emoji = get_dice_emoji();
  if (!emoji.empty()) {
    return telegram_api::make_object<telegram_api::inputStickerSetDice>(emoji);
  }

  UNREACHABLE();
  return nullptr;
}

}  // namespace td
