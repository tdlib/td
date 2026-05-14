// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/GuestBotTopDialog.h"

#include "td/telegram/ChannelId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/UserId.h"

#include "td/utils/tests.h"

namespace {

td::GuestBotTopDialogCandidate make_candidate() {
  td::GuestBotTopDialogCandidate candidate;
  candidate.guest_bot_via_dialog_id = td::DialogId(td::UserId(td::int64{111}));
  candidate.my_dialog_id = td::DialogId(td::UserId(td::int64{111}));
  candidate.guest_bot_dialog_id = td::DialogId(td::UserId(td::int64{222}));
  candidate.message_date = 100;
  candidate.is_forward = false;
  candidate.guest_bot_is_bot = true;
  return candidate;
}

TEST(W4GuestBotTopDialogRuntime, AcceptsMonotonicGuestBotUsageAndAdvancesWatermark) {
  auto candidate = make_candidate();
  td::int32 last_message_date = 0;

  ASSERT_TRUE(td::note_guest_bot_top_dialog_use(candidate, last_message_date));
  ASSERT_EQ(100, last_message_date);
}

TEST(W4GuestBotTopDialogRuntime, RejectsEqualOrOlderGuestBotUsageFailClosed) {
  auto candidate = make_candidate();
  td::int32 last_message_date = 120;

  candidate.message_date = 120;
  ASSERT_FALSE(td::note_guest_bot_top_dialog_use(candidate, last_message_date));
  ASSERT_EQ(120, last_message_date);

  candidate.message_date = 119;
  ASSERT_FALSE(td::note_guest_bot_top_dialog_use(candidate, last_message_date));
  ASSERT_EQ(120, last_message_date);
}

TEST(W4GuestBotTopDialogRuntime, RejectsForwardedOrMismatchedGuestUsageFailClosed) {
  auto candidate = make_candidate();
  td::int32 last_message_date = 0;

  candidate.is_forward = true;
  ASSERT_FALSE(td::note_guest_bot_top_dialog_use(candidate, last_message_date));
  ASSERT_EQ(0, last_message_date);

  candidate = make_candidate();
  candidate.guest_bot_via_dialog_id = td::DialogId(td::UserId(td::int64{333}));
  ASSERT_FALSE(td::note_guest_bot_top_dialog_use(candidate, last_message_date));
  ASSERT_EQ(0, last_message_date);
}

TEST(W4GuestBotTopDialogRuntime, RejectsNonBotOrNonUserGuestSendersFailClosed) {
  auto candidate = make_candidate();
  td::int32 last_message_date = 0;

  candidate.guest_bot_is_bot = false;
  ASSERT_FALSE(td::note_guest_bot_top_dialog_use(candidate, last_message_date));
  ASSERT_EQ(0, last_message_date);

  candidate = make_candidate();
  candidate.guest_bot_dialog_id = td::DialogId(td::ChannelId(td::int64{444}));
  ASSERT_FALSE(td::note_guest_bot_top_dialog_use(candidate, last_message_date));
  ASSERT_EQ(0, last_message_date);
}

}  // namespace