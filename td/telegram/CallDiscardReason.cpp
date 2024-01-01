//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/CallDiscardReason.h"

#include "td/utils/common.h"

namespace td {

CallDiscardReason get_call_discard_reason(const tl_object_ptr<telegram_api::PhoneCallDiscardReason> &reason) {
  if (reason == nullptr) {
    return CallDiscardReason::Empty;
  }
  switch (reason->get_id()) {
    case telegram_api::phoneCallDiscardReasonMissed::ID:
      return CallDiscardReason::Missed;
    case telegram_api::phoneCallDiscardReasonDisconnect::ID:
      return CallDiscardReason::Disconnected;
    case telegram_api::phoneCallDiscardReasonHangup::ID:
      return CallDiscardReason::HungUp;
    case telegram_api::phoneCallDiscardReasonBusy::ID:
      return CallDiscardReason::Declined;
    default:
      UNREACHABLE();
      return CallDiscardReason::Empty;
  }
}

tl_object_ptr<telegram_api::PhoneCallDiscardReason> get_input_phone_call_discard_reason(CallDiscardReason reason) {
  switch (reason) {
    case CallDiscardReason::Empty:
      return nullptr;
    case CallDiscardReason::Missed:
      return make_tl_object<telegram_api::phoneCallDiscardReasonMissed>();
    case CallDiscardReason::Disconnected:
      return make_tl_object<telegram_api::phoneCallDiscardReasonDisconnect>();
    case CallDiscardReason::HungUp:
      return make_tl_object<telegram_api::phoneCallDiscardReasonHangup>();
    case CallDiscardReason::Declined:
      return make_tl_object<telegram_api::phoneCallDiscardReasonBusy>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<td_api::CallDiscardReason> get_call_discard_reason_object(CallDiscardReason reason) {
  switch (reason) {
    case CallDiscardReason::Empty:
      return make_tl_object<td_api::callDiscardReasonEmpty>();
    case CallDiscardReason::Missed:
      return make_tl_object<td_api::callDiscardReasonMissed>();
    case CallDiscardReason::Disconnected:
      return make_tl_object<td_api::callDiscardReasonDisconnected>();
    case CallDiscardReason::HungUp:
      return make_tl_object<td_api::callDiscardReasonHungUp>();
    case CallDiscardReason::Declined:
      return make_tl_object<td_api::callDiscardReasonDeclined>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

}  // namespace td
