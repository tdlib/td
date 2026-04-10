//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class TranslationManager final : public Actor {
 public:
  TranslationManager(Td *td, ActorShared<> parent);

  void on_authorization_success();

  struct InputText {
    FormattedText text_;
    bool skip_bot_commands_ = true;
    int32 max_media_timestamp_ = -1;
  };

  void translate_text(td_api::object_ptr<td_api::formattedText> &&text, const string &to_language_code,
                      const string &tone, Promise<td_api::object_ptr<td_api::formattedText>> &&promise);

  void translate_text(InputText &&text, MessageFullId message_full_id, const string &to_language_code,
                      const string &tone, Promise<td_api::object_ptr<td_api::formattedText>> &&promise);

  void compose_message_with_ai(td_api::object_ptr<td_api::formattedText> &&text,
                               const string &translate_to_language_code, const string &tone, bool emojify,
                               Promise<td_api::object_ptr<td_api::formattedText>> &&promise);

  void proofread_message_with_ai(td_api::object_ptr<td_api::formattedText> &&text,
                                 Promise<td_api::object_ptr<td_api::fixedText>> &&promise);

  void on_update_ai_compose_styles(vector<string> &&ai_compose_styles);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  void start_up() final;

  void tear_down() final;

  Result<InputText> get_input_text(td_api::object_ptr<td_api::formattedText> &&text) const;

  void on_get_translated_texts(vector<telegram_api::object_ptr<telegram_api::textWithEntities>> texts,
                               bool skip_bot_commands, int32 max_media_timestamp,
                               Promise<td_api::object_ptr<td_api::formattedText>> &&promise);

  static string get_ai_compose_styles_key();

  td_api::object_ptr<td_api::updateTextCompositionStyles> get_update_text_composition_styles() const;

  void send_update_text_composition_styles() const;

  Td *td_;
  ActorShared<> parent_;

  vector<string> ai_compose_styles_;
};

}  // namespace td
