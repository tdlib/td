//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/CallDiscardReason.h"

#include "td/telegram/LinkManager.h"

#include "td/utils/common.h"

namespace td {

CallDiscardReason get_call_discard_reason(
    const telegram_api::object_ptr<telegram_api::PhoneCallDiscardReason> &reason) {
  CallDiscardReason result;
  if (reason != nullptr) {
    switch (reason->get_id()) {
      case telegram_api::phoneCallDiscardReasonMissed::ID:
        result.type_ = CallDiscardReason::Type::Missed;
        break;
      case telegram_api::phoneCallDiscardReasonDisconnect::ID:
        result.type_ = CallDiscardReason::Type::Disconnected;
        break;
      case telegram_api::phoneCallDiscardReasonHangup::ID:
        result.type_ = CallDiscardReason::Type::HungUp;
        break;
      case telegram_api::phoneCallDiscardReasonBusy::ID:
        result.type_ = CallDiscardReason::Type::Declined;
        break;
      case telegram_api::phoneCallDiscardReasonMigrateConferenceCall::ID:
        result.type_ = CallDiscardReason::Type::UpgradeToGroupCall;
        result.slug_ =
            static_cast<const telegram_api::phoneCallDiscardReasonMigrateConferenceCall *>(reason.get())->slug_;
        break;
      default:
        UNREACHABLE();
        break;
    }
  }
  return result;
}

telegram_api::object_ptr<telegram_api::PhoneCallDiscardReason> get_input_phone_call_discard_reason(
    CallDiscardReason reason) {
  switch (reason.type_) {
    case CallDiscardReason::Type::Empty:
      return nullptr;
    case CallDiscardReason::Type::Missed:
      return telegram_api::make_object<telegram_api::phoneCallDiscardReasonMissed>();
    case CallDiscardReason::Type::Disconnected:
      return telegram_api::make_object<telegram_api::phoneCallDiscardReasonDisconnect>();
    case CallDiscardReason::Type::HungUp:
      return telegram_api::make_object<telegram_api::phoneCallDiscardReasonHangup>();
    case CallDiscardReason::Type::Declined:
      return telegram_api::make_object<telegram_api::phoneCallDiscardReasonBusy>();
    case CallDiscardReason::Type::UpgradeToGroupCall:
      return telegram_api::make_object<telegram_api::phoneCallDiscardReasonMigrateConferenceCall>(reason.slug_);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::CallDiscardReason> get_call_discard_reason_object(CallDiscardReason reason) {
  switch (reason.type_) {
    case CallDiscardReason::Type::Empty:
      return td_api::make_object<td_api::callDiscardReasonEmpty>();
    case CallDiscardReason::Type::Missed:
      return td_api::make_object<td_api::callDiscardReasonMissed>();
    case CallDiscardReason::Type::Disconnected:
      return td_api::make_object<td_api::callDiscardReasonDisconnected>();
    case CallDiscardReason::Type::HungUp:
      return td_api::make_object<td_api::callDiscardReasonHungUp>();
    case CallDiscardReason::Type::Declined:
      return td_api::make_object<td_api::callDiscardReasonDeclined>();
    case CallDiscardReason::Type::UpgradeToGroupCall:
      return td_api::make_object<td_api::callDiscardReasonUpgradeToGroupCall>(
          LinkManager::get_group_call_invite_link(reason.slug_, false));
    default:
      UNREACHABLE();
      return nullptr;
  }
}

bool operator==(const CallDiscardReason &lhs, const CallDiscardReason &rhs) {
  return lhs.type_ == rhs.type_ && lhs.slug_ == rhs.slug_;
}

}  // namespace td
