//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BusinessAwayMessage.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/misc.h"

namespace td {

BusinessAwayMessage::BusinessAwayMessage(telegram_api::object_ptr<telegram_api::businessAwayMessage> away_message) {
  if (away_message == nullptr) {
    return;
  }
  shortcut_id_ = QuickReplyShortcutId(away_message->shortcut_id_);
  recipients_ = BusinessRecipients(std::move(away_message->recipients_));
  schedule_ = BusinessAwayMessageSchedule(std::move(away_message->schedule_));
}

BusinessAwayMessage::BusinessAwayMessage(td_api::object_ptr<td_api::businessAwayMessageSettings> away_message) {
  if (away_message == nullptr) {
    return;
  }
  shortcut_id_ = QuickReplyShortcutId(away_message->shortcut_id_);
  recipients_ = BusinessRecipients(std::move(away_message->recipients_));
  schedule_ = BusinessAwayMessageSchedule(std::move(away_message->schedule_));
}

td_api::object_ptr<td_api::businessAwayMessageSettings> BusinessAwayMessage::get_business_away_message_settings_object(
    Td *td) const {
  if (is_empty()) {
    return nullptr;
  }
  return td_api::make_object<td_api::businessAwayMessageSettings>(
      shortcut_id_.get(), recipients_.get_business_recipients_object(td),
      schedule_.get_business_away_message_schedule_object());
}

telegram_api::object_ptr<telegram_api::inputBusinessAwayMessage> BusinessAwayMessage::get_input_business_away_message(
    Td *td) const {
  int32 flags = 0;
  return telegram_api::make_object<telegram_api::inputBusinessAwayMessage>(
      flags, false /*ignored*/, shortcut_id_.get(), schedule_.get_input_business_away_message_schedule(),
      recipients_.get_input_business_recipients(td));
}

bool operator==(const BusinessAwayMessage &lhs, const BusinessAwayMessage &rhs) {
  return lhs.shortcut_id_ == rhs.shortcut_id_ && lhs.recipients_ == rhs.recipients_ && lhs.schedule_ == rhs.schedule_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const BusinessAwayMessage &away_message) {
  return string_builder << "away message " << away_message.shortcut_id_ << ' ' << away_message.recipients_ << ' '
                        << away_message.schedule_;
}

}  // namespace td
