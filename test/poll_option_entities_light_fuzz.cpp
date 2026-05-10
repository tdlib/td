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

td::uint64 next_value(td::uint64 &state) {
  state = state * 6364136223846793005ULL + 1442695040888963407ULL;
  return state;
}

TEST(PollOptionEntitiesLightFuzz, RandomizedEntityMixAlwaysProducesCustomEmojiOnlyInputEntities) {
  td::uint64 state = 0xC0FFEE1234ULL;

  for (int iteration = 0; iteration < 4000; iteration++) {
    td::FormattedText text;
    text.text = "012345678901234567890123456789";

    const auto entity_count = static_cast<int>(next_value(state) % 32u);
    for (int i = 0; i < entity_count; i++) {
      auto selector = static_cast<td::int32>(next_value(state) % 5u);
      auto offset = static_cast<td::int32>(next_value(state) % text.text.size());

      switch (selector) {
        case 0:
          text.entities.emplace_back(td::MessageEntity::Type::CustomEmoji, offset, 1,
                                     td::CustomEmojiId(td::int64{1000 + static_cast<td::int64>(i)}));
          break;
        case 1:
          text.entities.emplace_back(td::MessageEntity::Type::Bold, offset, 1, td::string());
          break;
        case 2:
          text.entities.emplace_back(td::MessageEntity::Type::Italic, offset, 1, td::string());
          break;
        case 3:
          text.entities.emplace_back(td::MessageEntity::Type::Underline, offset, 1, td::string());
          break;
        default:
          text.entities.emplace_back(td::MessageEntity::Type::Strikethrough, offset, 1, td::string());
          break;
      }
    }

    td::PollOption option(std::move(text), nullptr);
    auto answer = td::telegram_api::move_object_as<td::telegram_api::inputPollAnswer>(option.get_input_poll_answer());
    ASSERT_TRUE(answer->text_ != nullptr);

    for (const auto &entity : answer->text_->entities_) {
      ASSERT_EQ(td::telegram_api::messageEntityCustomEmoji::ID, entity->get_id());
    }
  }
}

}  // namespace
