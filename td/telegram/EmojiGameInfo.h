//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class Td;

class EmojiGameInfo {
  string game_hash_;
  int64 prev_stake_ = 0;
  int32 current_streak_ = 0;
  vector<int32> params_;

  friend bool operator==(const EmojiGameInfo &lhs, const EmojiGameInfo &rhs);

 public:
  EmojiGameInfo() = default;

  explicit EmojiGameInfo(telegram_api::object_ptr<telegram_api::messages_EmojiGameInfo> &&game_info);

  td_api::object_ptr<td_api::stakeDiceState> get_stake_dice_state_object(const Td *td) const;

  td_api::object_ptr<td_api::updateStakeDiceState> get_update_stake_dice_state_object(const Td *td) const;
};

bool operator==(const EmojiGameInfo &lhs, const EmojiGameInfo &rhs);

}  // namespace td
