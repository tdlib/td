//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AiComposeTone.h"

#include "td/telegram/Dependencies.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/base64.h"
#include "td/utils/logging.h"

namespace td {

AiComposeTone::AiComposeTone(telegram_api::object_ptr<telegram_api::AiComposeTone> &&tone_ptr) {
  CHECK(tone_ptr != nullptr);
  switch (tone_ptr->get_id()) {
    case telegram_api::aiComposeTone::ID: {
      auto tone = telegram_api::move_object_as<telegram_api::aiComposeTone>(tone_ptr);
      type_ = Type::Custom;
      slug_ = std::move(tone->slug_);
      custom_emoji_id_ = CustomEmojiId(tone->emoji_id_);
      title_ = std::move(tone->title_);
      is_creator_ = tone->creator_;
      id_ = tone->id_;
      access_hash_ = tone->access_hash_;
      install_count_ = tone->installs_count_;
      prompt_ = std::move(tone->prompt_);
      author_user_id_ = UserId(tone->author_id_);
      english_example_ = AiComposeToneExample(std::move(tone->example_english_));
      break;
    }
    case telegram_api::aiComposeToneDefault::ID: {
      auto tone = telegram_api::move_object_as<telegram_api::aiComposeToneDefault>(tone_ptr);
      type_ = Type::Default;
      slug_ = std::move(tone->tone_);
      custom_emoji_id_ = CustomEmojiId(tone->emoji_id_);
      title_ = std::move(tone->title_);
      break;
    }
    default:
      UNREACHABLE();
  }
  if (author_user_id_ != UserId() && !author_user_id_.is_valid()) {
    LOG(ERROR) << "Receive " << author_user_id_;
    author_user_id_ = {};
  }
  if (custom_emoji_id_ != CustomEmojiId() && !custom_emoji_id_.is_valid()) {
    LOG(ERROR) << "Receive " << custom_emoji_id_;
    custom_emoji_id_ = {};
  }
}

td_api::object_ptr<td_api::textCompositionStyle> AiComposeTone::get_text_composition_style_object(Td *td) const {
  return td_api::make_object<td_api::textCompositionStyle>(
      slug_, custom_emoji_id_.get(), title_, type_ == Type::Custom, is_creator_, install_count_, prompt_,
      td->user_manager_->get_user_id_object(author_user_id_, "textCompositionStyle"),
      english_example_.get_text_composition_style_example_object());
}

telegram_api::object_ptr<telegram_api::InputAiComposeTone> AiComposeTone::get_input_ai_compose_tone() const {
  switch (type_) {
    case Type::Default:
      return telegram_api::make_object<telegram_api::inputAiComposeToneDefault>(slug_);
    case Type::Custom:
      return telegram_api::make_object<telegram_api::inputAiComposeToneID>(id_, access_hash_);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

void AiComposeTone::add_dependencies(Dependencies &dependencies) const {
  dependencies.add(author_user_id_);
}

bool operator==(const AiComposeTone &lhs, const AiComposeTone &rhs) {
  return lhs.type_ == rhs.type_ && lhs.slug_ == rhs.slug_ && lhs.custom_emoji_id_ == rhs.custom_emoji_id_ &&
         lhs.title_ == rhs.title_ && lhs.is_creator_ == rhs.is_creator_ && lhs.id_ == rhs.id_ &&
         lhs.access_hash_ == rhs.access_hash_ && lhs.install_count_ == rhs.install_count_ &&
         lhs.prompt_ == rhs.prompt_ && lhs.author_user_id_ == rhs.author_user_id_ &&
         lhs.english_example_ == rhs.english_example_;
}

AiComposeTones::AiComposeTones(Td *td, telegram_api::object_ptr<telegram_api::aicompose_tones> &&tones) {
  CHECK(tones != nullptr);
  td->user_manager_->on_get_users(std::move(tones->users_), "AiComposeTones");
  for (auto &tone : tones->tones_) {
    tones_.emplace_back(std::move(tone));
  }
  hash_ = tones->hash_;
}

td_api::object_ptr<td_api::updateTextCompositionStyles> AiComposeTones::get_update_text_composition_styles_object(
    Td *td) const {
  return td_api::make_object<td_api::updateTextCompositionStyles>(
      transform(tones_, [td](const AiComposeTone &tone) { return tone.get_text_composition_style_object(td); }));
}

Result<telegram_api::object_ptr<telegram_api::InputAiComposeTone>> AiComposeTones::get_input_ai_compose_tone(
    const string &name) const {
  if (name.empty()) {
    return nullptr;
  }
  for (const auto &tone : tones_) {
    if (tone.has_name(name)) {
      return tone.get_input_ai_compose_tone();
    }
  }
  if (!name.empty() && is_base64url_characters(name)) {
    return telegram_api::make_object<telegram_api::inputAiComposeToneSlug>(name);
  }
  return Status::Error(400, "Style not found");
}

void AiComposeTones::add_dependencies(Dependencies &dependencies) const {
  for (const auto &tone : tones_) {
    tone.add_dependencies(dependencies);
  }
}

bool operator==(const AiComposeTones &lhs, const AiComposeTones &rhs) {
  return lhs.hash_ == rhs.hash_ && lhs.tones_ == rhs.tones_;
}

}  // namespace td
