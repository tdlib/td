//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AiComposeToneExample.h"
#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class Dependencies;
class Td;

class AiComposeTone {
  enum class Type : int32 { Default, Custom };
  Type type_ = Type::Default;
  string slug_;
  CustomEmojiId custom_emoji_id_;
  string title_;

  bool is_creator_ = false;
  int64 id_ = 0;
  int64 access_hash_ = 0;
  int32 install_count_ = 0;
  string prompt_;
  UserId author_user_id_;
  AiComposeToneExample english_example_;

  friend bool operator==(const AiComposeTone &lhs, const AiComposeTone &rhs);

 public:
  AiComposeTone() = default;

  explicit AiComposeTone(telegram_api::object_ptr<telegram_api::AiComposeTone> &&tone_ptr);

  td_api::object_ptr<td_api::textCompositionStyle> get_text_composition_style_object(Td *td) const;

  bool has_name(const string &name) const {
    return name == slug_;
  }

  telegram_api::object_ptr<telegram_api::InputAiComposeTone> get_input_ai_compose_tone() const;

  void add_dependencies(Dependencies &dependencies) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const AiComposeTone &lhs, const AiComposeTone &rhs);

inline bool operator!=(const AiComposeTone &lhs, const AiComposeTone &rhs) {
  return !(lhs == rhs);
}

class AiComposeTones {
  vector<AiComposeTone> tones_;
  int64 hash_ = 0;

  friend bool operator==(const AiComposeTones &lhs, const AiComposeTones &rhs);

 public:
  AiComposeTones() = default;

  AiComposeTones(Td *td, telegram_api::object_ptr<telegram_api::aicompose_tones> &&tones);

  td_api::object_ptr<td_api::updateTextCompositionStyles> get_update_text_composition_styles_object(Td *td) const;

  int64 get_hash() const {
    return hash_;
  }

  Result<telegram_api::object_ptr<telegram_api::InputAiComposeTone>> get_input_ai_compose_tone(
      const string &name) const;

  void add_dependencies(Dependencies &dependencies) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const AiComposeTones &lhs, const AiComposeTones &rhs);

inline bool operator!=(const AiComposeTones &lhs, const AiComposeTones &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
