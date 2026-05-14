// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/telegram/DialogId.h"

namespace td {

struct GuestBotTopDialogCandidate final {
  DialogId guest_bot_via_dialog_id;
  DialogId my_dialog_id;
  DialogId guest_bot_dialog_id;
  int32 message_date{0};
  bool is_forward{false};
  bool guest_bot_is_bot{false};
};

inline bool note_guest_bot_top_dialog_use(const GuestBotTopDialogCandidate &candidate,
                                          int32 &last_message_date) noexcept {
  if (candidate.is_forward || candidate.message_date <= 0 || !candidate.guest_bot_via_dialog_id.is_valid() ||
      candidate.guest_bot_via_dialog_id != candidate.my_dialog_id || !candidate.guest_bot_dialog_id.is_valid() ||
      candidate.guest_bot_dialog_id.get_type() != DialogType::User || !candidate.guest_bot_is_bot ||
      last_message_date >= candidate.message_date) {
    return false;
  }
  last_message_date = candidate.message_date;
  return true;
}

}  // namespace td