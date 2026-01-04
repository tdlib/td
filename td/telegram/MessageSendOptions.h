//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageEffectId.h"
#include "td/telegram/SuggestedPost.h"

#include "td/utils/common.h"

namespace td {

struct MessageSendOptions {
  bool disable_notification = false;
  bool from_background = false;
  bool update_stickersets_order = false;
  bool protect_content = false;
  bool allow_paid = false;
  bool only_preview = false;
  bool has_suggested_post = false;
  int32 schedule_date = 0;
  int32 schedule_repeat_period = 0;
  int32 sending_id = 0;
  MessageEffectId effect_id;
  int64 paid_message_star_count = 0;
  SuggestedPost suggested_post;

  MessageSendOptions() = default;
  MessageSendOptions(bool disable_notification, bool from_background, bool update_stickersets_order,
                     bool protect_content, bool allow_paid, bool only_preview, bool has_suggested_post,
                     int32 schedule_date, int32 schedule_repeat_period, int32 sending_id, MessageEffectId effect_id,
                     int64 paid_message_star_count, SuggestedPost &&suggested_post)
      : disable_notification(disable_notification)
      , from_background(from_background)
      , update_stickersets_order(update_stickersets_order)
      , protect_content(protect_content)
      , allow_paid(allow_paid)
      , only_preview(only_preview)
      , has_suggested_post(has_suggested_post)
      , schedule_date(schedule_date)
      , schedule_repeat_period(schedule_repeat_period)
      , sending_id(sending_id)
      , effect_id(effect_id)
      , paid_message_star_count(paid_message_star_count)
      , suggested_post(std::move(suggested_post)) {
  }
};

}  // namespace td
