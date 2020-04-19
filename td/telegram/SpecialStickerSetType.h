//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

struct SpecialStickerSetType {
  string type_;

  static string animated_emoji();

  static string animated_dice(const string &emoji);

  string get_dice_emoji() const;

  SpecialStickerSetType() = default;

  explicit SpecialStickerSetType(const telegram_api::object_ptr<telegram_api::InputStickerSet> &input_sticker_set);

  telegram_api::object_ptr<telegram_api::InputStickerSet> get_input_sticker_set() const;
};

}  // namespace td
