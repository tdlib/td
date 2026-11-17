//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AiComposeStyle.h"

#include "td/utils/logging.h"

namespace td {

AiComposeStyle::AiComposeStyle(telegram_api::object_ptr<telegram_api::inputAiComposeStyle> &&style) {
  if (style == nullptr) {
    return;
  }
  if (style->tone_ != nullptr) {
    switch (style->tone_->get_id()) {
      case telegram_api::inputAiComposeToneDefault::ID: {
        auto tone = telegram_api::move_object_as<telegram_api::inputAiComposeToneDefault>(style->tone_);
        tone_type_ = ToneType::Default;
        tone_name_ = std::move(tone->tone_);
        break;
      }
      case telegram_api::inputAiComposeToneID::ID: {
        auto tone = telegram_api::move_object_as<telegram_api::inputAiComposeToneID>(style->tone_);
        tone_type_ = ToneType::Id;
        tone_id_ = tone->id_;
        tone_access_hash_ = tone->access_hash_;
        break;
      }
      case telegram_api::inputAiComposeToneSlug::ID: {
        auto tone = telegram_api::move_object_as<telegram_api::inputAiComposeToneSlug>(style->tone_);
        tone_type_ = ToneType::Slug;
        tone_name_ = std::move(tone->slug_);
        break;
      }
      case telegram_api::inputAiComposeToneSingleUse::ID:
        LOG(ERROR) << "Receive " << to_string(style);
        break;
      default:
        UNREACHABLE();
    }
    if (tone_type_ != ToneType::Id && tone_name_.empty()) {
      LOG(ERROR) << "Receive AI compose tone without name";
      tone_type_ = ToneType::None;
    }
  }
  to_language_code_ = std::move(style->to_lang_);
  emojify_ = style->emojify_;
  proofread_ = style->proofread_;
}

telegram_api::object_ptr<telegram_api::inputAiComposeStyle> AiComposeStyle::get_input_ai_compose_style() const {
  telegram_api::object_ptr<telegram_api::InputAiComposeTone> input_tone;
  switch (tone_type_) {
    case ToneType::None:
      break;
    case ToneType::Default:
      input_tone = telegram_api::make_object<telegram_api::inputAiComposeToneDefault>(tone_name_);
      break;
    case ToneType::Id:
      input_tone = telegram_api::make_object<telegram_api::inputAiComposeToneID>(tone_id_, tone_access_hash_);
      break;
    case ToneType::Slug:
      input_tone = telegram_api::make_object<telegram_api::inputAiComposeToneSlug>(tone_name_);
      break;
    default:
      UNREACHABLE();
  }
  return telegram_api::make_object<telegram_api::inputAiComposeStyle>(0, emojify_, proofread_, std::move(input_tone),
                                                                      to_language_code_);
}

void AiComposeStyle::copy_tone_name_if_same(const AiComposeStyle &other) {
  if (tone_type_ == ToneType::Id && other.tone_type_ == ToneType::Id && tone_id_ == other.tone_id_) {
    tone_name_ = other.tone_name_;
  }
}

td_api::object_ptr<td_api::textCompositionParameters> AiComposeStyle::get_text_composition_parameters_object() const {
  if (is_empty()) {
    return nullptr;
  }
  return td_api::make_object<td_api::textCompositionParameters>(to_language_code_, tone_name_, emojify_, proofread_);
}

bool operator==(const AiComposeStyle &lhs, const AiComposeStyle &rhs) {
  return lhs.tone_type_ == rhs.tone_type_ && lhs.tone_name_ == rhs.tone_name_ && lhs.tone_id_ == rhs.tone_id_ &&
         lhs.tone_access_hash_ == rhs.tone_access_hash_ && lhs.to_language_code_ == rhs.to_language_code_ &&
         lhs.emojify_ == rhs.emojify_ && lhs.proofread_ == rhs.proofread_;
}

td_api::object_ptr<td_api::textCompositionParameters> get_text_composition_parameters_object(
    const unique_ptr<AiComposeStyle> &ai_compose_style) {
  if (ai_compose_style == nullptr) {
    return nullptr;
  }
  return ai_compose_style->get_text_composition_parameters_object();
}

}  // namespace td
