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

TEST(PollOptionEntitiesStress, LargeEntityPayloadsRemainDeterministicAcrossRepeatedConstruction) {
  using enum td::MessageEntity::Type;

  td::FormattedText base_text;
  base_text.text = td::string(1024, 'x');

  for (td::int32 i = 0; i < 1024; i++) {
    if ((i % 7) == 0) {
      base_text.entities.emplace_back(CustomEmoji, i, 1,
                                      td::CustomEmojiId(td::int64{900000 + static_cast<td::int64>(i)}));
    } else if ((i % 2) == 0) {
      base_text.entities.emplace_back(Bold, i, 1, td::string());
    } else {
      base_text.entities.emplace_back(Italic, i, 1, td::string());
    }
  }

  td::size_t expected_custom_count = 0;
  for (const auto &entity : base_text.entities) {
    if (entity.type == CustomEmoji) {
      expected_custom_count++;
    }
  }

  for (int iteration = 0; iteration < 1000; iteration++) {
    auto text = base_text;
    td::PollOption option(std::move(text), nullptr);
    auto answer = td::telegram_api::move_object_as<td::telegram_api::inputPollAnswer>(option.get_input_poll_answer());

    ASSERT_TRUE(answer->text_ != nullptr);
    ASSERT_EQ(expected_custom_count, answer->text_->entities_.size());
    for (const auto &entity : answer->text_->entities_) {
      ASSERT_EQ(td::telegram_api::messageEntityCustomEmoji::ID, entity->get_id());
    }
  }
}

}  // namespace
