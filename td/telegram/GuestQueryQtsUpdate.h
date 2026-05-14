// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

enum class GuestQueryQtsUpdateResult : std::uint8_t {
  InvalidQueryId,
  EmptyMessage,
  UpdateSent,
};

template <class ConvertMessage, class SendUpdate>
GuestQueryQtsUpdateResult dispatch_guest_query_qts_update(
    int64 query_id, vector<tl_object_ptr<telegram_api::Message>> &&reference_messages,
    tl_object_ptr<telegram_api::Message> &&message, ConvertMessage &&convert_message, SendUpdate &&send_update) {
  if (query_id <= 0) {
    return GuestQueryQtsUpdateResult::InvalidQueryId;
  }

  vector<td_api::object_ptr<td_api::message>> converted_reference_messages;
  converted_reference_messages.reserve(reference_messages.size());
  for (auto &reference_message : reference_messages) {
    auto converted_reference_message = convert_message(std::move(reference_message));
    if (converted_reference_message != nullptr) {
      converted_reference_messages.push_back(std::move(converted_reference_message));
    }
  }

  auto converted_message = convert_message(std::move(message));
  if (converted_message == nullptr) {
    return GuestQueryQtsUpdateResult::EmptyMessage;
  }

  send_update(query_id, std::move(converted_message), std::move(converted_reference_messages));
  return GuestQueryQtsUpdateResult::UpdateSent;
}

}  // namespace td