// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/MessageContent.h"  // IWYU pragma: keep
#include "td/telegram/MessageEntity.h"
#include "td/telegram/PollOption.h"
#include "td/telegram/telegram_api.h"
#include "td/utils/tests.h"

namespace {

TEST(PollOptionEntitiesAdversarial, MixedEntityPayloadNeverLeaksNonCustomEntitiesToInputPollAnswer) {
  td::FormattedText text;
  text.text = "abcdefghijklmnopqrstuvwxyz";

  td::size_t expected_custom_count = 0;
  for (td::int32 i = 0; i < 26; i++) {
    if ((i % 4) == 0) {
      text.entities.emplace_back(td::MessageEntity::Type::CustomEmoji, i, 1,
                                 td::CustomEmojiId(td::int64{100000 + static_cast<td::int64>(i)}));
      expected_custom_count++;
    } else if ((i % 2) == 0) {
      text.entities.emplace_back(td::MessageEntity::Type::Bold, i, 1, td::string());
    } else {
      text.entities.emplace_back(td::MessageEntity::Type::Italic, i, 1, td::string());
    }
  }

  td::PollOption option(std::move(text), nullptr);
  auto answer = td::telegram_api::move_object_as<td::telegram_api::inputPollAnswer>(option.get_input_poll_answer());
  ASSERT_TRUE(answer->text_ != nullptr);
  ASSERT_EQ(expected_custom_count, answer->text_->entities_.size());

  for (const auto &entity : answer->text_->entities_) {
    ASSERT_EQ(td::telegram_api::messageEntityCustomEmoji::ID, entity->get_id());
  }
}

}  // namespace
