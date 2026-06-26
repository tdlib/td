//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TranslationManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AiComposeTone.hpp"
#include "td/telegram/AiComposeToneExample.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/DiffText.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

namespace td {

class TranslateTextQuery final : public Td::ResultHandler {
  Promise<vector<telegram_api::object_ptr<telegram_api::textWithEntities>>> promise_;

 public:
  explicit TranslateTextQuery(Promise<vector<telegram_api::object_ptr<telegram_api::textWithEntities>>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(vector<FormattedText> &&texts, MessageFullId message_full_id, const string &to_language_code, string tone) {
    int32 flags = 0;
    if (tone == "neutral") {
      tone.clear();
    }
    if (!tone.empty()) {
      flags |= telegram_api::messages_translateText::TONE_MASK;
    }
    if (message_full_id.get_message_id().is_valid()) {
      CHECK(texts.size() == 1u);
      flags |= telegram_api::messages_translateText::PEER_MASK;
      auto input_peer = td_->dialog_manager_->get_input_peer(message_full_id.get_dialog_id(), AccessRights::Read);
      CHECK(input_peer != nullptr);
      auto message_ids = {message_full_id.get_message_id().get_server_message_id().get()};
      send_query(G()->net_query_creator().create(telegram_api::messages_translateText(
          flags, std::move(input_peer), std::move(message_ids), Auto(), to_language_code, tone)));
    } else {
      flags |= telegram_api::messages_translateText::TEXT_MASK;
      auto input_texts = transform(std::move(texts), [user_manager = td_->user_manager_.get()](FormattedText &&text) {
        return get_input_text_with_entities(user_manager, std::move(text), "TranslateTextQuery");
      });
      send_query(G()->net_query_creator().create(telegram_api::messages_translateText(
          flags, nullptr, vector<int32>{}, std::move(input_texts), to_language_code, tone)));
    }
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_translateText>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for TranslateTextQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr->result_));
  }

  void on_error(Status status) final {
    if (status.message() == "INPUT_TEXT_EMPTY") {
      vector<telegram_api::object_ptr<telegram_api::textWithEntities>> result;
      result.push_back(telegram_api::make_object<telegram_api::textWithEntities>(string(), Auto()));
      return promise_.set_value(std::move(result));
    }
    promise_.set_error(std::move(status));
  }
};

class TranslateRichMessageQuery final : public Td::ResultHandler {
  Promise<vector<telegram_api::object_ptr<telegram_api::richMessage>>> promise_;

 public:
  explicit TranslateRichMessageQuery(Promise<vector<telegram_api::object_ptr<telegram_api::richMessage>>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(vector<telegram_api::object_ptr<telegram_api::InputRichMessage>> &&rich_messages,
            MessageFullId message_full_id, const string &to_language_code, string tone) {
    int32 flags = 0;
    if (tone == "neutral") {
      tone.clear();
    }
    if (!tone.empty()) {
      flags |= telegram_api::messages_translateRichMessage::TONE_MASK;
    }
    if (message_full_id.get_message_id().is_valid()) {
      CHECK(rich_messages.size() == 1u);
      flags |= telegram_api::messages_translateRichMessage::PEER_MASK;
      auto input_peer = td_->dialog_manager_->get_input_peer(message_full_id.get_dialog_id(), AccessRights::Read);
      CHECK(input_peer != nullptr);
      auto message_ids = {message_full_id.get_message_id().get_server_message_id().get()};
      send_query(G()->net_query_creator().create(telegram_api::messages_translateRichMessage(
          flags, std::move(input_peer), std::move(message_ids), Auto(), to_language_code, tone)));
    } else {
      flags |= telegram_api::messages_translateRichMessage::TEXT_MASK;
      send_query(G()->net_query_creator().create(telegram_api::messages_translateRichMessage(
          flags, nullptr, vector<int32>{}, std::move(rich_messages), to_language_code, tone)));
    }
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_translateRichMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for TranslateRichMessageQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr->result_));
  }

  void on_error(Status status) final {
    if (status.message() == "INPUT_TEXT_EMPTY") {
      vector<telegram_api::object_ptr<telegram_api::richMessage>> result;
      result.push_back(telegram_api::make_object<telegram_api::richMessage>());
      return promise_.set_value(std::move(result));
    }
    promise_.set_error(std::move(status));
  }
};

class GetAiComposeTonesQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit GetAiComposeTonesQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::aicompose_getTones(hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::aicompose_getTones>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetAiComposeTonesQuery: " << to_string(ptr);
    td_->translation_manager_->on_get_ai_compose_tones(std::move(ptr));
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for GetAiComposeTonesQuery: " << status;
    promise_.set_value(Unit());  // ignore the error
  }
};

class CreateToneQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::textCompositionStyle>> promise_;

 public:
  explicit CreateToneQuery(Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &title, CustomEmojiId custom_emoji_id, const string &prompt, bool show_creator) {
    send_query(G()->net_query_creator().create(
        telegram_api::aicompose_createTone(0, show_creator, custom_emoji_id.get(), title, prompt)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::aicompose_createTone>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CreateToneQuery: " << to_string(ptr);
    td_->translation_manager_->reload_ai_compose_tones(PromiseCreator::lambda(
        [promise = std::move(promise_), style = AiComposeTone(std::move(ptr)).get_text_composition_style_object(td_)](
            Unit) mutable { promise.set_value(std::move(style)); }));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for CreateToneQuery: " << status;
    promise_.set_error(std::move(status));
  }
};

class UpdateToneQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::textCompositionStyle>> promise_;

 public:
  explicit UpdateToneQuery(Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::InputAiComposeTone> &&input_tone, const string &title,
            CustomEmojiId custom_emoji_id, const string &prompt, bool show_creator) {
    int32 flags = telegram_api::aicompose_updateTone::DISPLAY_AUTHOR_MASK |
                  telegram_api::aicompose_updateTone::EMOJI_ID_MASK | telegram_api::aicompose_updateTone::TITLE_MASK |
                  telegram_api::aicompose_updateTone::PROMPT_MASK;
    send_query(G()->net_query_creator().create(telegram_api::aicompose_updateTone(
        flags, std::move(input_tone), show_creator, custom_emoji_id.get(), title, prompt)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::aicompose_updateTone>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for UpdateToneQuery: " << to_string(ptr);
    td_->translation_manager_->reload_ai_compose_tones(PromiseCreator::lambda(
        [promise = std::move(promise_), style = AiComposeTone(std::move(ptr)).get_text_composition_style_object(td_)](
            Unit) mutable { promise.set_value(std::move(style)); }));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for UpdateToneQuery: " << status;
    promise_.set_error(std::move(status));
  }
};

class DeleteToneQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DeleteToneQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::InputAiComposeTone> &&input_tone) {
    send_query(G()->net_query_creator().create(telegram_api::aicompose_deleteTone(std::move(input_tone))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::aicompose_deleteTone>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->translation_manager_->reload_ai_compose_tones(std::move(promise_));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for DeleteToneQuery: " << status;
    promise_.set_error(std::move(status));
  }
};

class GetToneQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::textCompositionStyle>> promise_;

 public:
  explicit GetToneQuery(Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::InputAiComposeTone> &&input_tone) {
    send_query(G()->net_query_creator().create(telegram_api::aicompose_getTone(std::move(input_tone))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::aicompose_getTone>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetToneQuery: " << to_string(ptr);
    switch (ptr->get_id()) {
      case telegram_api::aicompose_tonesNotModified::ID:
        break;
      case telegram_api::aicompose_tones::ID: {
        auto update = AiComposeTones(td_, telegram_api::move_object_as<telegram_api::aicompose_tones>(ptr))
                          .get_update_text_composition_styles_object(td_);
        if (update->styles_.size() == 1u) {
          return promise_.set_value(std::move(update->styles_[0]));
        }
        break;
      }
      default:
        UNREACHABLE();
    }
    LOG(ERROR) << "Receive " << to_string(ptr);
    promise_.set_value(nullptr);
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for GetToneQuery: " << status;
    promise_.set_error(std::move(status));
  }
};

class GetToneExampleQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::textCompositionStyleExample>> promise_;

 public:
  explicit GetToneExampleQuery(Promise<td_api::object_ptr<td_api::textCompositionStyleExample>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::InputAiComposeTone> &&input_tone, int32 num) {
    send_query(G()->net_query_creator().create(telegram_api::aicompose_getToneExample(std::move(input_tone), num)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::aicompose_getToneExample>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetToneExampleQuery: " << to_string(ptr);
    promise_.set_value(AiComposeToneExample(std::move(ptr)).get_text_composition_style_example_object());
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for GetToneExampleQuery: " << status;
    promise_.set_error(std::move(status));
  }
};

class SaveToneQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SaveToneQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(telegram_api::object_ptr<telegram_api::InputAiComposeTone> &&input_tone, bool unsave) {
    send_query(G()->net_query_creator().create(telegram_api::aicompose_saveTone(std::move(input_tone), unsave)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::aicompose_saveTone>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->translation_manager_->reload_ai_compose_tones(std::move(promise_));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for SaveToneQuery: " << status;
    promise_.set_error(std::move(status));
  }
};

class ComposeMessageWithAiQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::formattedText>> promise_;
  bool skip_bot_commands_;
  int32 max_media_timestamp_;

 public:
  explicit ComposeMessageWithAiQuery(Promise<td_api::object_ptr<td_api::formattedText>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const TranslationManager::InputText &text, const string &translate_to_language_code,
            telegram_api::object_ptr<telegram_api::InputAiComposeTone> &&input_tone, bool emojify) {
    skip_bot_commands_ = text.skip_bot_commands_;
    max_media_timestamp_ = text.max_media_timestamp_;

    int32 flags = 0;
    if (!translate_to_language_code.empty()) {
      flags |= telegram_api::messages_composeMessageWithAI::TRANSLATE_TO_LANG_MASK;
    }
    if (input_tone != nullptr) {
      flags |= telegram_api::messages_composeMessageWithAI::TONE_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_composeMessageWithAI(
        flags, false, emojify,
        get_input_text_with_entities(td_->user_manager_.get(), text.text_, "ComposeMessageWithAiQuery"),
        translate_to_language_code, std::move(input_tone))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_composeMessageWithAI>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ComposeMessageWithAiQuery: " << to_string(ptr);
    auto formatted_text = get_formatted_text(td_->user_manager_.get(), std::move(ptr->result_text_),
                                             max_media_timestamp_ == -1, true, "ComposeMessageWithAiQuery");
    promise_.set_value(
        get_formatted_text_object(td_->user_manager_.get(), formatted_text, skip_bot_commands_, max_media_timestamp_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ComposeRichMessageWithAiQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::richMessage>> promise_;
  bool skip_bot_commands_;

 public:
  explicit ComposeRichMessageWithAiQuery(Promise<td_api::object_ptr<td_api::richMessage>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(bool has_message, const TranslationManager::InputRichMessage &message,
            const string &translate_to_language_code,
            telegram_api::object_ptr<telegram_api::InputAiComposeTone> &&input_tone, bool emojify, bool proofread) {
    skip_bot_commands_ = message.skip_bot_commands_;
    telegram_api::object_ptr<telegram_api::InputRichMessage> input_rich_message;

    int32 flags = 0;
    if (!translate_to_language_code.empty()) {
      flags |= telegram_api::messages_composeMessageWithAI::TRANSLATE_TO_LANG_MASK;
    }
    if (input_tone != nullptr) {
      flags |= telegram_api::messages_composeMessageWithAI::TONE_MASK;
    }
    if (has_message) {
      flags |= telegram_api::messages_composeRichMessageWithAI::TEXT_MASK;
      input_rich_message = message.message_.get_input_rich_message(td_);
      if (input_rich_message == nullptr) {
        return on_error(Status::Error(400, "Invalid rich message specified"));
      }
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_composeRichMessageWithAI(
        flags, proofread, emojify, std::move(input_rich_message), translate_to_language_code, std::move(input_tone))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_composeRichMessageWithAI>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ComposeRichMessageWithAiQuery: " << to_string(ptr);
    auto rich_message = RichMessage(td_, std::move(ptr->result_), DialogId());
    promise_.set_value(rich_message.get_rich_message_object(td_, skip_bot_commands_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ProofreadMessageWithAiQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::fixedText>> promise_;
  bool skip_bot_commands_;
  int32 max_media_timestamp_;

 public:
  explicit ProofreadMessageWithAiQuery(Promise<td_api::object_ptr<td_api::fixedText>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const TranslationManager::InputText &text) {
    skip_bot_commands_ = text.skip_bot_commands_;
    max_media_timestamp_ = text.max_media_timestamp_;

    send_query(G()->net_query_creator().create(telegram_api::messages_composeMessageWithAI(
        0, true, false,
        get_input_text_with_entities(td_->user_manager_.get(), text.text_, "ProofreadMessageWithAiQuery"), string(),
        nullptr)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_composeMessageWithAI>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ProofreadMessageWithAiQuery: " << to_string(ptr);
    auto formatted_text = get_formatted_text(td_->user_manager_.get(), std::move(ptr->result_text_),
                                             max_media_timestamp_ == -1, true, "ProofreadMessageWithAiQuery");
    auto diff_text = DiffText(std::move(ptr->diff_text_));
    promise_.set_value(td_api::make_object<td_api::fixedText>(
        get_formatted_text_object(td_->user_manager_.get(), formatted_text, skip_bot_commands_, max_media_timestamp_),
        diff_text.get_diff_text_object()));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

TranslationManager::TranslationManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void TranslationManager::start_up() {
  if (td_->auth_manager_->is_authorized() && !td_->auth_manager_->is_bot()) {
    auto ai_compose_tones_log_event_string = G()->td_db()->get_binlog_pmc()->get(get_ai_compose_tones_key());
    if (!ai_compose_tones_log_event_string.empty()) {
      if (log_event_parse(ai_compose_tones_, ai_compose_tones_log_event_string).is_error()) {
        ai_compose_tones_ = {};
      } else {
        Dependencies dependencies;
        ai_compose_tones_.add_dependencies(dependencies);
        if (!dependencies.resolve_force(td_, "AiComposeTones")) {
          ai_compose_tones_ = {};
        }
      }
    }
    send_update_text_composition_styles();
    if (ai_compose_tones_ == AiComposeTones()) {
      reload_ai_compose_tones(Auto());
    }
  }
}

void TranslationManager::tear_down() {
  parent_.reset();
}

void TranslationManager::on_authorization_success() {
  if (!td_->auth_manager_->is_bot()) {
    send_update_text_composition_styles();
    reload_ai_compose_tones(Auto());
  }
}

Result<TranslationManager::InputText> TranslationManager::get_input_text(
    td_api::object_ptr<td_api::formattedText> &&text) const {
  if (text == nullptr) {
    return Status::Error(400, "Text must be non-empty");
  }
  InputText input_text;
  for (const auto &entity : text->entities_) {
    if (entity == nullptr || entity->type_ == nullptr) {
      continue;
    }

    switch (entity->type_->get_id()) {
      case td_api::textEntityTypeBotCommand::ID:
        input_text.skip_bot_commands_ = false;
        break;
      case td_api::textEntityTypeMediaTimestamp::ID:
        input_text.max_media_timestamp_ =
            td::max(input_text.max_media_timestamp_,
                    static_cast<const td_api::textEntityTypeMediaTimestamp *>(entity->type_.get())->media_timestamp_);
        break;
      default:
        // nothing to do
        break;
    }
  }

  TRY_RESULT(entities, get_message_entities(td_->user_manager_.get(), std::move(text->entities_)));
  TRY_STATUS(fix_formatted_text(text->text_, entities, true, true, true, true, true, true));
  input_text.text_ = FormattedText{std::move(text->text_), std::move(entities)};
  return std::move(input_text);
}

void TranslationManager::translate_text(td_api::object_ptr<td_api::formattedText> &&text,
                                        const string &to_language_code, const string &tone,
                                        Promise<td_api::object_ptr<td_api::formattedText>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_text, get_input_text(std::move(text)));
  translate_text(std::move(input_text), MessageFullId(), to_language_code, tone, std::move(promise));
}

void TranslationManager::translate_text(InputText &&text, MessageFullId message_full_id, const string &to_language_code,
                                        const string &tone,
                                        Promise<td_api::object_ptr<td_api::formattedText>> &&promise) {
  vector<FormattedText> texts;
  texts.push_back(std::move(text.text_));

  if (tone != string() && tone != "formal" && tone != "neutral" && tone != "casual") {
    return promise.set_error(400, "Invalid tone specified");
  }

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), skip_bot_commands = text.skip_bot_commands_,
       max_media_timestamp = text.max_media_timestamp_, promise = std::move(promise)](
          Result<vector<telegram_api::object_ptr<telegram_api::textWithEntities>>> result) mutable {
        if (result.is_error()) {
          return promise.set_error(result.move_as_error());
        }
        send_closure(actor_id, &TranslationManager::on_get_translated_texts, result.move_as_ok(), skip_bot_commands,
                     max_media_timestamp, std::move(promise));
      });

  td_->create_handler<TranslateTextQuery>(std::move(query_promise))
      ->send(std::move(texts), message_full_id, to_language_code, tone);
}

void TranslationManager::on_get_translated_texts(vector<telegram_api::object_ptr<telegram_api::textWithEntities>> texts,
                                                 bool skip_bot_commands, int32 max_media_timestamp,
                                                 Promise<td_api::object_ptr<td_api::formattedText>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  if (texts.size() != 1u) {
    if (texts.empty()) {
      return promise.set_error(500, "Translation failed");
    }
    return promise.set_error(500, "Receive invalid number of results");
  }
  auto formatted_text = get_formatted_text(td_->user_manager_.get(), std::move(texts[0]), max_media_timestamp == -1,
                                           true, "on_get_translated_texts");
  promise.set_value(
      get_formatted_text_object(td_->user_manager_.get(), formatted_text, skip_bot_commands, max_media_timestamp));
}

Result<TranslationManager::InputRichMessage> TranslationManager::get_input_rich_message(
    td_api::object_ptr<td_api::inputRichMessage> &&message) const {
  TRY_RESULT(rich_message, RichMessage::get_rich_message(td_, DialogId(), std::move(message), false));

  InputRichMessage input_rich_message;
  input_rich_message.skip_bot_commands_ = !rich_message.has_bot_commands();
  input_rich_message.message_ = std::move(rich_message);
  return std::move(input_rich_message);
}

void TranslationManager::translate_rich_message(td_api::object_ptr<td_api::inputRichMessage> &&message,
                                                const string &to_language_code, const string &tone,
                                                Promise<td_api::object_ptr<td_api::richMessage>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_rich_message, get_input_rich_message(std::move(message)));
  translate_rich_message(std::move(input_rich_message), MessageFullId(), to_language_code, tone, std::move(promise));
}

void TranslationManager::translate_rich_message(InputRichMessage &&rich_message, MessageFullId message_full_id,
                                                const string &to_language_code, const string &tone,
                                                Promise<td_api::object_ptr<td_api::richMessage>> &&promise) {
  auto input_rich_message = rich_message.message_.get_input_rich_message(td_);
  if (input_rich_message == nullptr) {
    return promise.set_error(400, "Invalid rich message specified");
  }
  vector<telegram_api::object_ptr<telegram_api::InputRichMessage>> input_rich_messages;
  input_rich_messages.push_back(std::move(input_rich_message));

  if (tone != string() && tone != "formal" && tone != "neutral" && tone != "casual") {
    return promise.set_error(400, "Invalid tone specified");
  }

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), skip_bot_commands = rich_message.skip_bot_commands_, promise = std::move(promise)](
          Result<vector<telegram_api::object_ptr<telegram_api::richMessage>>> result) mutable {
        if (result.is_error()) {
          return promise.set_error(result.move_as_error());
        }
        send_closure(actor_id, &TranslationManager::on_get_translated_rich_messages, result.move_as_ok(),
                     skip_bot_commands, std::move(promise));
      });

  td_->create_handler<TranslateRichMessageQuery>(std::move(query_promise))
      ->send(std::move(input_rich_messages), message_full_id, to_language_code, tone);
}

void TranslationManager::on_get_translated_rich_messages(
    vector<telegram_api::object_ptr<telegram_api::richMessage>> rich_messages, bool skip_bot_commands,
    Promise<td_api::object_ptr<td_api::richMessage>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  if (rich_messages.size() != 1u) {
    if (rich_messages.empty()) {
      return promise.set_error(500, "Translation failed");
    }
    return promise.set_error(500, "Receive invalid number of results");
  }
  auto rich_message = RichMessage(td_, std::move(rich_messages[0]), DialogId());
  promise.set_value(rich_message.get_rich_message_object(td_, skip_bot_commands));
}

void TranslationManager::compose_message_with_ai(td_api::object_ptr<td_api::formattedText> &&text,
                                                 const string &translate_to_language_code, const string &tone,
                                                 bool emojify,
                                                 Promise<td_api::object_ptr<td_api::formattedText>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_text, get_input_text(std::move(text)));
  TRY_RESULT_PROMISE(promise, input_tone, ai_compose_tones_.get_input_ai_compose_tone(tone));
  td_->create_handler<ComposeMessageWithAiQuery>(std::move(promise))
      ->send(input_text, translate_to_language_code, std::move(input_tone), emojify);
}

void TranslationManager::proofread_message_with_ai(td_api::object_ptr<td_api::formattedText> &&text,
                                                   Promise<td_api::object_ptr<td_api::fixedText>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_text, get_input_text(std::move(text)));
  td_->create_handler<ProofreadMessageWithAiQuery>(std::move(promise))->send(input_text);
}

void TranslationManager::compose_rich_message_with_ai(td_api::object_ptr<td_api::inputRichMessage> &&message,
                                                      const string &translate_to_language_code, const string &tone,
                                                      const string &custom_prompt, bool emojify,
                                                      Promise<td_api::object_ptr<td_api::richMessage>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_rich_message, get_input_rich_message(std::move(message)));
  telegram_api::object_ptr<telegram_api::InputAiComposeTone> input_tone;
  if (custom_prompt.empty()) {
    TRY_RESULT_PROMISE_ASSIGN(promise, input_tone, ai_compose_tones_.get_input_ai_compose_tone(tone));
  } else {
    input_tone = telegram_api::make_object<telegram_api::inputAiComposeToneSingleUse>(custom_prompt);
  }
  td_->create_handler<ComposeRichMessageWithAiQuery>(std::move(promise))
      ->send(true, input_rich_message, translate_to_language_code, std::move(input_tone), emojify, false);
}

void TranslationManager::create_rich_message_with_ai(const string &prompt, const string &language_code, bool emojify,
                                                     Promise<td_api::object_ptr<td_api::richMessage>> &&promise) {
  td_->create_handler<ComposeRichMessageWithAiQuery>(std::move(promise))
      ->send(false, InputRichMessage(), language_code,
             telegram_api::make_object<telegram_api::inputAiComposeToneSingleUse>(prompt), emojify, false);
}

void TranslationManager::proofread_rich_message_with_ai(td_api::object_ptr<td_api::inputRichMessage> &&message,
                                                        Promise<td_api::object_ptr<td_api::richMessage>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_rich_message, get_input_rich_message(std::move(message)));
  td_->create_handler<ComposeRichMessageWithAiQuery>(std::move(promise))
      ->send(true, input_rich_message, string(), nullptr, false, true);
}

string TranslationManager::get_ai_compose_tones_key() {
  return "ai_compose_styles";
}

void TranslationManager::reload_ai_compose_tones(Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_authorized() && !td_->auth_manager_->is_bot()) {
    td_->create_handler<GetAiComposeTonesQuery>(std::move(promise))->send(ai_compose_tones_.get_hash());
  }
}

void TranslationManager::on_get_ai_compose_tones(telegram_api::object_ptr<telegram_api::aicompose_Tones> &&tones_ptr) {
  CHECK(tones_ptr != nullptr);
  if (!td_->auth_manager_->is_authorized()) {
    return;
  }
  switch (tones_ptr->get_id()) {
    case telegram_api::aicompose_tonesNotModified::ID:
      break;
    case telegram_api::aicompose_tones::ID: {
      auto ai_compose_tones =
          AiComposeTones(td_, telegram_api::move_object_as<telegram_api::aicompose_tones>(tones_ptr));
      if (ai_compose_tones == ai_compose_tones_) {
        break;
      }
      ai_compose_tones_ = std::move(ai_compose_tones);
      G()->td_db()->get_binlog_pmc()->set(get_ai_compose_tones_key(),
                                          log_event_store(ai_compose_tones_).as_slice().str());
      send_update_text_composition_styles();
      break;
    }
    default:
      UNREACHABLE();
  }
}

void TranslationManager::create_tone(const string &title, CustomEmojiId custom_emoji_id, const string &prompt,
                                     bool show_creator,
                                     Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise) {
  td_->user_manager_->get_me([actor_id = actor_id(this), title, custom_emoji_id, prompt, show_creator,
                              promise = std::move(promise)](Unit) mutable {
    send_closure(actor_id, &TranslationManager::do_create_tone, title, custom_emoji_id, prompt, show_creator,
                 std::move(promise));
  });
}

void TranslationManager::do_create_tone(const string &title, CustomEmojiId custom_emoji_id, const string &prompt,
                                        bool show_creator,
                                        Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->create_handler<CreateToneQuery>(std::move(promise))->send(title, custom_emoji_id, prompt, show_creator);
}

void TranslationManager::update_tone(const string &name, const string &title, CustomEmojiId custom_emoji_id,
                                     const string &prompt, bool show_creator,
                                     Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise) {
  td_->user_manager_->get_me([actor_id = actor_id(this), name, title, custom_emoji_id, prompt, show_creator,
                              promise = std::move(promise)](Unit) mutable {
    send_closure(actor_id, &TranslationManager::do_update_tone, name, title, custom_emoji_id, prompt, show_creator,
                 std::move(promise));
  });
}

void TranslationManager::do_update_tone(const string &name, const string &title, CustomEmojiId custom_emoji_id,
                                        const string &prompt, bool show_creator,
                                        Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_tone, ai_compose_tones_.get_input_ai_compose_tone(name));
  td_->create_handler<UpdateToneQuery>(std::move(promise))
      ->send(std::move(input_tone), title, custom_emoji_id, prompt, show_creator);
}

void TranslationManager::delete_tone(const string &name, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_tone, ai_compose_tones_.get_input_ai_compose_tone(name));
  td_->create_handler<DeleteToneQuery>(std::move(promise))->send(std::move(input_tone));
}

void TranslationManager::search_tone(const string &name,
                                     Promise<td_api::object_ptr<td_api::textCompositionStyle>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_tone, ai_compose_tones_.get_input_ai_compose_tone(name));
  td_->create_handler<GetToneQuery>(std::move(promise))->send(std::move(input_tone));
}

void TranslationManager::get_tone_example(const string &name, int32 num,
                                          Promise<td_api::object_ptr<td_api::textCompositionStyleExample>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_tone, ai_compose_tones_.get_input_ai_compose_tone(name));
  td_->create_handler<GetToneExampleQuery>(std::move(promise))->send(std::move(input_tone), num);
}

void TranslationManager::add_tone(const string &name, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_tone, ai_compose_tones_.get_input_ai_compose_tone(name));
  td_->create_handler<SaveToneQuery>(std::move(promise))->send(std::move(input_tone), false);
}

void TranslationManager::remove_tone(const string &name, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_tone, ai_compose_tones_.get_input_ai_compose_tone(name));
  td_->create_handler<SaveToneQuery>(std::move(promise))->send(std::move(input_tone), true);
}

void TranslationManager::send_update_text_composition_styles() const {
  send_closure(G()->td(), &Td::send_update, ai_compose_tones_.get_update_text_composition_styles_object(td_));
}

void TranslationManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }

  updates.push_back(ai_compose_tones_.get_update_text_composition_styles_object(td_));
}

}  // namespace td
