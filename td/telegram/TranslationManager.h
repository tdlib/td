//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AiComposeTone.h"
#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/RichMessage.h"
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

  static telegram_api::object_ptr<telegram_api::InputAiComposeTone> clone_input_ai_compose_tone(
      const telegram_api::object_ptr<telegram_api::InputAiComposeTone> &input_tone);

  struct InputText {
    FormattedText text_;
    bool skip_bot_commands_ = true;
    int32 max_media_timestamp_ = -1;
  };

  void translate_text(td_api::object_ptr<td_api::formattedText> &&text, const string &to_language_code,
                      const string &tone, Promise<td_api::object_ptr<td_api::formattedText>> &&promise);

  void translate_text(InputText &&text, MessageFullId message_full_id, const string &to_language_code,
                      const string &tone, Promise<td_api::object_ptr<td_api::formattedText>> &&promise);

  struct InputRichMessage {
    RichMessage message_;
    bool skip_bot_commands_ = true;
  };

  void translate_rich_message(td_api::object_ptr<td_api::inputRichMessage> &&message, const string &to_language_code,
                              const string &tone, Promise<td_api::object_ptr<td_api::richMessage>> &&promise);

  void translate_rich_message(InputRichMessage &&input_rich_message, MessageFullId message_full_id,
                              const string &to_language_code, const string &tone,
                              Promise<td_api::object_ptr<td_api::richMessage>> &&promise);

  void do_translate_rich_message(RichMessage &&rich_message, MessageFullId message_full_id,
                                 const string &to_language_code, const string &tone,
                                 Promise<vector<telegram_api::object_ptr<telegram_api::richMessage>>> &&promise);

  void compose_message_with_ai(td_api::object_ptr<td_api::formattedText> &&text,
                               const string &translate_to_language_code, const string &tone, bool emojify,
                               Promise<td_api::object_ptr<td_api::formattedText>> &&promise);

  void do_compose_rich_message_with_ai(bool has_message, TranslationManager::InputRichMessage &&message,
                                       const string &translate_to_language_code,
                                       telegram_api::object_ptr<telegram_api::InputAiComposeTone> &&input_tone,
                                       bool emojify, bool proofread,
                                       Promise<td_api::object_ptr<td_api::richMessage>> &&promise);

  void compose_rich_message_with_ai(td_api::object_ptr<td_api::inputRichMessage> &&message,
                                    const string &translate_to_language_code, const string &tone,
                                    const string &custom_prompt, bool emojify,
                                    Promise<td_api::object_ptr<td_api::richMessage>> &&promise);

  void create_rich_message_with_ai(const string &prompt, const string &language_code, bool emojify,
                                   Promise<td_api::object_ptr<td_api::richMessage>> &&promise);

  void proofread_message_with_ai(td_api::object_ptr<td_api::formattedText> &&text,
                                 Promise<td_api::object_ptr<td_api::fixedText>> &&promise);

  void proofread_rich_message_with_ai(td_api::object_ptr<td_api::inputRichMessage> &&message,
                                      Promise<td_api::object_ptr<td_api::richMessage>> &&promise);

  void reload_ai_compose_tones(Promise<Unit> &&promise);

  void on_get_ai_compose_tones(telegram_api::object_ptr<telegram_api::aicompose_Tones> &&tones_ptr);

  void create_tone(const string &title, CustomEmojiId custom_emoji_id, const string &prompt, bool show_creator,
                   Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise);

  void update_tone(const string &name, const string &title, CustomEmojiId custom_emoji_id, const string &prompt,
                   bool show_creator, Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise);

  void delete_tone(const string &name, Promise<Unit> &&promise);

  void search_tone(const string &name, Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise);

  void get_tone_example(const string &name, int32 num,
                        Promise<td_api::object_ptr<td_api::textCompositionStyleExample>> &&promise);

  void add_tone(const string &name, Promise<Unit> &&promise);

  void remove_tone(const string &name, Promise<Unit> &&promise);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  void start_up() final;

  void tear_down() final;

  Result<InputText> get_input_text(td_api::object_ptr<td_api::formattedText> &&text) const;

  Result<InputRichMessage> get_input_rich_message(td_api::object_ptr<td_api::inputRichMessage> &&text) const;

  void on_get_translated_texts(vector<telegram_api::object_ptr<telegram_api::textWithEntities>> texts,
                               bool skip_bot_commands, int32 max_media_timestamp,
                               Promise<td_api::object_ptr<td_api::formattedText>> &&promise);

  void on_get_translated_rich_messages(vector<telegram_api::object_ptr<telegram_api::richMessage>> rich_messages,
                                       bool skip_bot_commands,
                                       Promise<td_api::object_ptr<td_api::richMessage>> &&promise);

  void do_create_tone(const string &title, CustomEmojiId custom_emoji_id, const string &prompt, bool show_creator,
                      Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise);

  void do_update_tone(const string &name, const string &title, CustomEmojiId custom_emoji_id, const string &prompt,
                      bool show_creator, Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise);

  static string get_ai_compose_tones_key();

  void send_update_text_composition_styles() const;

  Td *td_;
  ActorShared<> parent_;

  AiComposeTones ai_compose_tones_;
};

}  // namespace td
