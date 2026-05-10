// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/MessageContent.h"  // IWYU pragma: keep
#include "td/telegram/MessageEntity.h"
#include "td/telegram/PollOption.h"

#include "td/utils/tests.h"

namespace {

td::PollOption make_base_option() {
  td::FormattedText text;
  text.text = "xyz";
  text.entities.emplace_back(td::MessageEntity::Type::CustomEmoji, 1, 1, td::CustomEmojiId(td::int64{777}));
  return td::PollOption(std::move(text), nullptr);
}

TEST(PollOptionEntitiesIntegration, EqualityIgnoresVoteCountAndRecentVotersButKeepsTextAndDataInvariants) {
  auto lhs = make_base_option();
  auto rhs = make_base_option();

  rhs.voter_count_ = 100;
  rhs.is_chosen_ = true;
  rhs.recent_voter_dialog_ids_.push_back(td::DialogId());

  ASSERT_TRUE(lhs == rhs);
  ASSERT_FALSE(lhs != rhs);

  td::FormattedText changed_text;
  changed_text.text = "xyz";
  changed_text.entities.emplace_back(td::MessageEntity::Type::CustomEmoji, 0, 1, td::CustomEmojiId(td::int64{888}));
  td::PollOption changed(std::move(changed_text), nullptr);

  ASSERT_FALSE(lhs == changed);
  ASSERT_TRUE(lhs != changed);
}

}  // namespace
