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

TEST(PollOptionEntitiesContract, ConstructorKeepsOnlyCustomEmojiEntities) {
  td::FormattedText text;
  text.text = "abc";
  td::MessageEntity bold;
  bold.type = td::MessageEntity::Type::Bold;
  bold.offset = 0;
  bold.length = 3;
  text.entities.push_back(std::move(bold));
  text.entities.emplace_back(td::MessageEntity::Type::CustomEmoji, 1, 1, td::CustomEmojiId(td::int64{123456789}));
  td::MessageEntity italic;
  italic.type = td::MessageEntity::Type::Italic;
  italic.offset = 0;
  italic.length = 1;
  text.entities.push_back(std::move(italic));

  td::PollOption option(std::move(text), nullptr);

  auto answer = td::telegram_api::move_object_as<td::telegram_api::inputPollAnswer>(option.get_input_poll_answer());
  ASSERT_TRUE(answer->text_ != nullptr);
  ASSERT_EQ("abc", answer->text_->text_);
  ASSERT_TRUE(answer->media_ == nullptr);
  ASSERT_EQ(1u, answer->text_->entities_.size());
  ASSERT_EQ(td::telegram_api::messageEntityCustomEmoji::ID, answer->text_->entities_[0]->get_id());

  auto custom_entity =
      static_cast<const td::telegram_api::messageEntityCustomEmoji *>(answer->text_->entities_[0].get());
  ASSERT_EQ(123456789, custom_entity->document_id_);
}

}  // namespace
