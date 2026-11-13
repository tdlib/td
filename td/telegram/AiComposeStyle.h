//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class AiComposeStyle {
  enum class ToneType : int32 { None, Default, Id, Slug };
  ToneType tone_type_ = ToneType::None;
  string tone_name_;
  int64 tone_id_ = 0;
  int64 tone_access_hash_ = 0;
  string to_language_code_;
  bool emojify_ = false;
  bool proofread_ = false;

  friend bool operator==(const AiComposeStyle &lhs, const AiComposeStyle &rhs);

 public:
  AiComposeStyle() = default;

  explicit AiComposeStyle(telegram_api::object_ptr<telegram_api::inputAiComposeStyle> &&style);

  bool is_empty() const {
    return tone_type_ == ToneType::None && to_language_code_.empty() && !emojify_ && !proofread_;
  }

  bool need_tone_name() const {
    return tone_type_ == ToneType::Id && tone_name_.empty();
  }

  int64 get_tone_id() const {
    return tone_id_;
  }

  int64 get_tone_access_hash() const {
    return tone_access_hash_;
  }

  void set_tone_name(string tone_name) {
    tone_name_ = std::move(tone_name);
  }

  void copy_tone_name_if_same(const AiComposeStyle &other);

  telegram_api::object_ptr<telegram_api::inputAiComposeStyle> get_input_ai_compose_style() const;

  td_api::object_ptr<td_api::textCompositionParameters> get_text_composition_parameters_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const AiComposeStyle &lhs, const AiComposeStyle &rhs);

inline bool operator!=(const AiComposeStyle &lhs, const AiComposeStyle &rhs) {
  return !(lhs == rhs);
}

td_api::object_ptr<td_api::textCompositionParameters> get_text_composition_parameters_object(
    const unique_ptr<AiComposeStyle> &ai_compose_style);

}  // namespace td
