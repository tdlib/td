//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageSendOptions.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageContentType.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/SuggestedPost.h"
#include "td/telegram/Td.h"

namespace td {

MessageSendOptions::MessageSendOptions(bool disable_notification, bool from_background, bool update_stickersets_order,
                                       bool protect_content, bool allow_paid, bool only_preview, int32 schedule_date,
                                       int32 schedule_repeat_period, int32 sending_id, MessageEffectId effect_id,
                                       int64 paid_message_star_count, unique_ptr<SuggestedPost> &&suggested_post)
    : disable_notification(disable_notification)
    , from_background(from_background)
    , update_stickersets_order(update_stickersets_order)
    , protect_content(protect_content)
    , allow_paid(allow_paid)
    , only_preview(only_preview)
    , schedule_date(schedule_date)
    , schedule_repeat_period(schedule_repeat_period)
    , sending_id(sending_id)
    , effect_id(effect_id)
    , paid_message_star_count(paid_message_star_count)
    , suggested_post(std::move(suggested_post)) {
}

MessageSendOptions::~MessageSendOptions() = default;

Result<std::pair<int32, int32>> MessageSendOptions::get_message_schedule_date(
    td_api::object_ptr<td_api::MessageSchedulingState> &&scheduling_state, bool allow_repeat_period) {
  if (scheduling_state == nullptr) {
    return std::make_pair(0, 0);
  }

  switch (scheduling_state->get_id()) {
    case td_api::messageSchedulingStateSendWhenVideoProcessed::ID:
      return Status::Error(400, "Can't force video processing");
    case td_api::messageSchedulingStateSendWhenOnline::ID: {
      auto send_date = SCHEDULE_WHEN_ONLINE_DATE;
      return std::make_pair(send_date, 0);
    }
    case td_api::messageSchedulingStateSendAtDate::ID: {
      auto send_at_date = td_api::move_object_as<td_api::messageSchedulingStateSendAtDate>(scheduling_state);
      auto send_date = send_at_date->send_date_;
      if (send_date <= 0) {
        return Status::Error(400, "Invalid send date specified");
      }
      if (send_date <= G()->unix_time() + 10) {
        return std::make_pair(0, 0);
      }
      if (send_date - G()->unix_time() > 367 * 86400) {
        return Status::Error(400, "Send date is too far in the future");
      }
      auto repeat_period = send_at_date->repeat_period_;
      if (!allow_repeat_period && repeat_period != 0) {
        return Status::Error(400, "Repeated scheduled messages aren't supported");
      }
      if (repeat_period != 0 && repeat_period != 86400 && repeat_period != 7 * 86400 && repeat_period != 14 * 86400 &&
          repeat_period != 30 * 86400 && repeat_period != 91 * 86400 && repeat_period != 182 * 86400 &&
          repeat_period != 365 * 86400) {
        if (!G()->is_test_dc() || (repeat_period != 60 && repeat_period != 300)) {
          return Status::Error(400, "Invalid message repeat period specified");
        }
      }
      return std::make_pair(send_date, repeat_period);
    }
    default:
      UNREACHABLE();
      return std::make_pair(0, 0);
  }
}

td_api::object_ptr<td_api::MessageSchedulingState> MessageSendOptions::get_message_scheduling_state_object(
    int32 send_date, int32 repeat_period, bool video_processing_pending) {
  if (video_processing_pending) {
    return td_api::make_object<td_api::messageSchedulingStateSendWhenVideoProcessed>(send_date);
  }
  if (send_date == SCHEDULE_WHEN_ONLINE_DATE) {
    return td_api::make_object<td_api::messageSchedulingStateSendWhenOnline>();
  }
  return td_api::make_object<td_api::messageSchedulingStateSendAtDate>(send_date, repeat_period);
}

Status MessageSendOptions::check_paid_message_star_count(Td *td, int64 &paid_message_star_count, int32 message_count) {
  if (paid_message_star_count < 0 || paid_message_star_count > 1000000) {
    return Status::Error(400, "Invalid price for paid message specified");
  }
  CHECK(message_count > 0);
  if (paid_message_star_count % message_count != 0) {
    return Status::Error(400, "Invalid price for paid messages specified");
  }
  if (paid_message_star_count > 0 && !td->star_manager_->has_owned_star_count(paid_message_star_count)) {
    return Status::Error(400, "Have not enough Telegram Stars");
  }
  paid_message_star_count /= message_count;
  return Status::OK();
}

Result<MessageSendOptions> MessageSendOptions::get_message_send_options(
    Td *td, DialogId dialog_id, td_api::object_ptr<td_api::messageSendOptions> &&options,
    bool allow_update_stickersets_order, bool allow_effect, bool allow_suggested_post, bool allow_repeat_period,
    int32 message_count) {
  MessageSendOptions result;
  if (options == nullptr) {
    return std::move(result);
  }

  result.disable_notification = options->disable_notification_;
  result.from_background = options->from_background_;
  if (allow_update_stickersets_order) {
    result.update_stickersets_order = options->update_order_of_installed_sticker_sets_;
  }
  if (td->auth_manager_->is_bot()) {
    result.protect_content = options->protect_content_;
    result.allow_paid = options->allow_paid_broadcast_;
  } else {
    result.paid_message_star_count = options->paid_message_star_count_;
    TRY_STATUS(check_paid_message_star_count(td, result.paid_message_star_count, message_count));
  }
  result.only_preview = options->only_preview_;
  TRY_RESULT(schedule_date, get_message_schedule_date(std::move(options->scheduling_state_), allow_repeat_period));
  result.schedule_date = schedule_date.first;
  result.schedule_repeat_period = schedule_date.second;
  result.sending_id = options->sending_id_;

  if (result.schedule_date != 0) {
    auto dialog_type = dialog_id.get_type();
    if (dialog_type == DialogType::SecretChat) {
      return Status::Error(400, "Can't schedule messages in secret chats");
    }
    if (td->auth_manager_->is_bot()) {
      return Status::Error(400, "Bots can't send scheduled messages");
    }

    if (result.schedule_date == SCHEDULE_WHEN_ONLINE_DATE) {
      if (dialog_type != DialogType::User) {
        return Status::Error(400, "Messages can be scheduled till online only in private chats");
      }
      if (dialog_id == td->dialog_manager_->get_my_dialog_id()) {
        return Status::Error(400, "Can't scheduled till online messages in chat with self");
      }
    }
    if (result.paid_message_star_count > 0) {
      return Status::Error(400, "Can't schedule paid messages");
    }
    if (td->dialog_manager_->is_admined_monoforum_channel(dialog_id)) {
      return Status::Error(400, "Can't schedule messages in channel direct messages chats");
    }
  }
  if (options->effect_id_ != 0) {
    auto dialog_type = dialog_id.get_type();
    if (dialog_type != DialogType::User) {
      return Status::Error(400, "Can't use message effects in the chat");
    }
    if (!allow_effect) {
      return Status::Error(400, "Can't use message effects in the method");
    }
    result.effect_id = MessageEffectId(options->effect_id_);
  }
  TRY_RESULT(suggested_post, SuggestedPost::get_suggested_post(td, std::move(options->suggested_post_info_)));
  if (suggested_post != nullptr) {
    if (!allow_suggested_post) {
      return Status::Error(400, "Can't send suggested posts with the method");
    }
    if (!td->dialog_manager_->is_monoforum_channel(dialog_id)) {
      return Status::Error(400, "Suggested posts can be sent only to channel direct messages");
    }
    result.suggested_post = std::move(suggested_post);
  }

  return std::move(result);
}

Status MessageSendOptions::can_use_for(const unique_ptr<MessageContent> &content, MessageSelfDestructType ttl) const {
  if (schedule_date != 0) {
    if (ttl.is_valid()) {
      return Status::Error(400, "Can't send scheduled self-destructing messages");
    }
    if (content->get_type() == MessageContentType::LiveLocation) {
      return Status::Error(400, "Can't send scheduled live location messages");
    }
  }
  return Status::OK();
}

Status MessageSendOptions::can_use_for(const InputMessageContent &content) const {
  return can_use_for(content.content, content.ttl);
}

}  // namespace td
