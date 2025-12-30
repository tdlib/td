//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/EmojiGameInfo.h"

#include "td/telegram/OptionManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TonAmount.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"

namespace td {

EmojiGameInfo::EmojiGameInfo(telegram_api::object_ptr<telegram_api::messages_EmojiGameInfo> &&game_info) {
  CHECK(game_info != nullptr);
  switch (game_info->get_id()) {
    case telegram_api::messages_emojiGameUnavailable::ID:
      break;
    case telegram_api::messages_emojiGameDiceInfo::ID: {
      auto info = telegram_api::move_object_as<telegram_api::messages_emojiGameDiceInfo>(game_info);
      auto is_bad = info->game_hash_.empty() || info->prev_stake_ < 0 || info->current_streak_ < 0 ||
                    info->current_streak_ >= 3 || info->params_.size() != 7u;
      for (auto param : info->params_) {
        if (param < 0) {
          is_bad = true;
        }
      }
      if (is_bad) {
        LOG(ERROR) << "Receive " << to_string(info);
        break;
      }
      game_hash_ = std::move(info->game_hash_);
      prev_stake_ = TonAmount::get_ton_count(info->prev_stake_, false);
      current_streak_ = info->current_streak_;
      params_ = std::move(info->params_);
      break;
    }
    default:
      UNREACHABLE();
  }
}

td_api::object_ptr<td_api::stakeDiceState> EmojiGameInfo::get_stake_dice_state_object(const Td *td) const {
  if (game_hash_.empty()) {
    return td_api::make_object<td_api::stakeDiceState>();
  }
  CHECK(params_.size() == 7u);
  auto suggested_amounts =
      transform(full_split(td->option_manager_->get_option_string("ton_stakedice_stake_suggested_amounts"), ','),
                to_integer<int64>);
  return td_api::make_object<td_api::stakeDiceState>(game_hash_, prev_stake_, std::move(suggested_amounts),
                                                     current_streak_,
                                                     vector<int32>(params_.begin(), params_.begin() + 6), params_[6]);
}

td_api::object_ptr<td_api::updateStakeDiceState> EmojiGameInfo::get_update_stake_dice_state_object(const Td *td) const {
  return td_api::make_object<td_api::updateStakeDiceState>(get_stake_dice_state_object(td));
}

bool operator==(const EmojiGameInfo &lhs, const EmojiGameInfo &rhs) {
  return lhs.game_hash_ == rhs.game_hash_ && lhs.prev_stake_ == rhs.prev_stake_ &&
         lhs.current_streak_ == rhs.current_streak_ && lhs.params_ == rhs.params_;
}

}  // namespace td
