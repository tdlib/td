//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/int_types.h"

namespace td {

enum class CallDiscardReason : int32 { Empty, Missed, Disconnected, HungUp, Declined };

CallDiscardReason get_call_discard_reason(const tl_object_ptr<telegram_api::PhoneCallDiscardReason> &reason);

tl_object_ptr<telegram_api::PhoneCallDiscardReason> get_input_phone_call_discard_reason(CallDiscardReason reason);

tl_object_ptr<td_api::CallDiscardReason> get_call_discard_reason_object(CallDiscardReason reason);

}  // namespace td
