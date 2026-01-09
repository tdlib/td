//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageEffectId.h"
#include "td/telegram/MessageSelfDestructType.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <utility>

namespace td {

struct InputMessageContent;
class MessageContent;
class SuggestedPost;
class Td;

class MessageSendOptions {
  static constexpr int32 SCHEDULE_WHEN_ONLINE_DATE = 2147483646;

 public:
  bool disable_notification = false;
  bool from_background = false;
  bool update_stickersets_order = false;
  bool protect_content = false;
  bool allow_paid = false;
  bool only_preview = false;
  int32 schedule_date = 0;
  int32 schedule_repeat_period = 0;
  int32 sending_id = 0;
  MessageEffectId effect_id;
  int64 paid_message_star_count = 0;
  unique_ptr<SuggestedPost> suggested_post;

  MessageSendOptions() = default;
  MessageSendOptions(bool disable_notification, bool from_background, bool update_stickersets_order,
                     bool protect_content, bool allow_paid, bool only_preview, int32 schedule_date,
                     int32 schedule_repeat_period, int32 sending_id, MessageEffectId effect_id,
                     int64 paid_message_star_count, unique_ptr<SuggestedPost> &&suggested_post);
  MessageSendOptions(const MessageSendOptions &) = delete;
  MessageSendOptions &operator=(const MessageSendOptions &) = delete;
  MessageSendOptions(MessageSendOptions &&) = default;
  MessageSendOptions &operator=(MessageSendOptions &&) = default;
  ~MessageSendOptions();

  static Result<std::pair<int32, int32>> get_message_schedule_date(
      td_api::object_ptr<td_api::MessageSchedulingState> &&scheduling_state, bool allow_repeat_period);

  static td_api::object_ptr<td_api::MessageSchedulingState> get_message_scheduling_state_object(
      int32 send_date, int32 repeat_period, bool video_processing_pending);

  static Status check_paid_message_star_count(Td *td, int64 &paid_message_star_count, int32 message_count);

  static Result<MessageSendOptions> get_message_send_options(Td *td, DialogId dialog_id,
                                                             td_api::object_ptr<td_api::messageSendOptions> &&options,
                                                             bool allow_update_stickersets_order, bool allow_effect,
                                                             bool allow_suggested_post, bool allow_repeat_period,
                                                             int32 message_count);

  Status can_use_for(const unique_ptr<MessageContent> &content, MessageSelfDestructType ttl) const;

  Status can_use_for(const InputMessageContent &content) const;
};

}  // namespace td
