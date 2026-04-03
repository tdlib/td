//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TranslationManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/DiffText.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
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

class ComposeMessageWithAiQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::formattedText>> promise_;
  bool skip_bot_commands_;
  int32 max_media_timestamp_;

 public:
  explicit ComposeMessageWithAiQuery(Promise<td_api::object_ptr<td_api::formattedText>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(FormattedText &&text, const string &translate_to_language_code, string tone, bool emojify,
            bool skip_bot_commands, int32 max_media_timestamp) {
    skip_bot_commands_ = skip_bot_commands;
    max_media_timestamp_ = max_media_timestamp;

    int32 flags = 0;
    if (!translate_to_language_code.empty()) {
      flags |= telegram_api::messages_composeMessageWithAI::TRANSLATE_TO_LANG_MASK;
    }
    if (!tone.empty()) {
      flags |= telegram_api::messages_composeMessageWithAI::CHANGE_TONE_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_composeMessageWithAI(
        flags, false, emojify,
        get_input_text_with_entities(td_->user_manager_.get(), std::move(text), "ComposeMessageWithAiQuery"),
        translate_to_language_code, tone)));
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

class ProofreadMessageWithAiQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::fixedText>> promise_;
  bool skip_bot_commands_;
  int32 max_media_timestamp_;

 public:
  explicit ProofreadMessageWithAiQuery(Promise<td_api::object_ptr<td_api::fixedText>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(FormattedText &&text, bool skip_bot_commands, int32 max_media_timestamp) {
    skip_bot_commands_ = skip_bot_commands;
    max_media_timestamp_ = max_media_timestamp;

    send_query(G()->net_query_creator().create(telegram_api::messages_composeMessageWithAI(
        0, true, false,
        get_input_text_with_entities(td_->user_manager_.get(), std::move(text), "ProofreadMessageWithAiQuery"),
        string(), string())));
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
    auto ai_compose_styles_log_event_string = G()->td_db()->get_binlog_pmc()->get(get_ai_compose_styles_key());
    if (!ai_compose_styles_log_event_string.empty()) {
      log_event_parse(ai_compose_styles_, ai_compose_styles_log_event_string).ensure();
    }
    send_update_text_composition_styles();
  }
}

void TranslationManager::tear_down() {
  parent_.reset();
}

void TranslationManager::on_authorization_success() {
  if (!td_->auth_manager_->is_bot()) {
    send_update_text_composition_styles();
  }
}

void TranslationManager::translate_text(td_api::object_ptr<td_api::formattedText> &&text,
                                        const string &to_language_code, const string &tone,
                                        Promise<td_api::object_ptr<td_api::formattedText>> &&promise) {
  if (text == nullptr) {
    return promise.set_error(400, "Text must be non-empty");
  }
  bool skip_bot_commands = true;
  int32 max_media_timestamp = -1;
  for (const auto &entity : text->entities_) {
    if (entity == nullptr || entity->type_ == nullptr) {
      continue;
    }

    switch (entity->type_->get_id()) {
      case td_api::textEntityTypeBotCommand::ID:
        skip_bot_commands = false;
        break;
      case td_api::textEntityTypeMediaTimestamp::ID:
        max_media_timestamp =
            td::max(max_media_timestamp,
                    static_cast<const td_api::textEntityTypeMediaTimestamp *>(entity->type_.get())->media_timestamp_);
        break;
      default:
        // nothing to do
        break;
    }
  }

  TRY_RESULT_PROMISE(promise, entities, get_message_entities(td_->user_manager_.get(), std::move(text->entities_)));
  TRY_STATUS_PROMISE(promise, fix_formatted_text(text->text_, entities, true, true, true, true, true, true));

  translate_text(FormattedText{std::move(text->text_), std::move(entities)}, skip_bot_commands, max_media_timestamp,
                 MessageFullId(), to_language_code, tone, std::move(promise));
}

void TranslationManager::translate_text(FormattedText text, bool skip_bot_commands, int32 max_media_timestamp,
                                        MessageFullId message_full_id, const string &to_language_code,
                                        const string &tone,
                                        Promise<td_api::object_ptr<td_api::formattedText>> &&promise) {
  vector<FormattedText> texts;
  texts.push_back(std::move(text));

  if (tone != string() && tone != "formal" && tone != "neutral" && tone != "casual") {
    return promise.set_error(400, "Invalid tone specified");
  }

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), skip_bot_commands, max_media_timestamp, promise = std::move(promise)](
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

void TranslationManager::compose_message_with_ai(td_api::object_ptr<td_api::formattedText> &&text,
                                                 const string &translate_to_language_code, const string &tone,
                                                 bool emojify,
                                                 Promise<td_api::object_ptr<td_api::formattedText>> &&promise) {
  if (text == nullptr) {
    return promise.set_error(400, "Text must be non-empty");
  }

  bool skip_bot_commands = true;
  int32 max_media_timestamp = -1;
  for (const auto &entity : text->entities_) {
    if (entity == nullptr || entity->type_ == nullptr) {
      continue;
    }

    switch (entity->type_->get_id()) {
      case td_api::textEntityTypeBotCommand::ID:
        skip_bot_commands = false;
        break;
      case td_api::textEntityTypeMediaTimestamp::ID:
        max_media_timestamp =
            td::max(max_media_timestamp,
                    static_cast<const td_api::textEntityTypeMediaTimestamp *>(entity->type_.get())->media_timestamp_);
        break;
      default:
        // nothing to do
        break;
    }
  }

  TRY_RESULT_PROMISE(promise, entities, get_message_entities(td_->user_manager_.get(), std::move(text->entities_)));
  TRY_STATUS_PROMISE(promise, fix_formatted_text(text->text_, entities, true, true, true, true, true, true));
  td_->create_handler<ComposeMessageWithAiQuery>(std::move(promise))
      ->send(FormattedText{std::move(text->text_), std::move(entities)}, translate_to_language_code, tone, emojify,
             skip_bot_commands, max_media_timestamp);
}

void TranslationManager::proofread_message_with_ai(td_api::object_ptr<td_api::formattedText> &&text,
                                                   Promise<td_api::object_ptr<td_api::fixedText>> &&promise) {
  if (text == nullptr) {
    return promise.set_error(400, "Text must be non-empty");
  }

  bool skip_bot_commands = true;
  int32 max_media_timestamp = -1;
  for (const auto &entity : text->entities_) {
    if (entity == nullptr || entity->type_ == nullptr) {
      continue;
    }

    switch (entity->type_->get_id()) {
      case td_api::textEntityTypeBotCommand::ID:
        skip_bot_commands = false;
        break;
      case td_api::textEntityTypeMediaTimestamp::ID:
        max_media_timestamp =
            td::max(max_media_timestamp,
                    static_cast<const td_api::textEntityTypeMediaTimestamp *>(entity->type_.get())->media_timestamp_);
        break;
      default:
        // nothing to do
        break;
    }
  }

  TRY_RESULT_PROMISE(promise, entities, get_message_entities(td_->user_manager_.get(), std::move(text->entities_)));
  TRY_STATUS_PROMISE(promise, fix_formatted_text(text->text_, entities, true, true, true, true, true, true));
  td_->create_handler<ProofreadMessageWithAiQuery>(std::move(promise))
      ->send(FormattedText{std::move(text->text_), std::move(entities)}, skip_bot_commands, max_media_timestamp);
}

string TranslationManager::get_ai_compose_styles_key() {
  return "ai_compose_styles";
}

void TranslationManager::on_update_ai_compose_styles(vector<string> &&ai_compose_styles) {
  if (ai_compose_styles == ai_compose_styles_) {
    return;
  }
  ai_compose_styles_ = std::move(ai_compose_styles);
  G()->td_db()->get_binlog_pmc()->set(get_ai_compose_styles_key(),
                                      log_event_store(ai_compose_styles_).as_slice().str());
  if (td_->auth_manager_->is_authorized()) {
    send_update_text_composition_styles();
  }
}

td_api::object_ptr<td_api::updateTextCompositionStyles> TranslationManager::get_update_text_composition_styles() const {
  CHECK(ai_compose_styles_.size() % 3 == 0);
  auto result = td_api::make_object<td_api::updateTextCompositionStyles>();
  for (size_t i = 0; i < ai_compose_styles_.size(); i += 3) {
    result->styles_.push_back(td_api::make_object<td_api::textCompositionStyle>(
        ai_compose_styles_[i], to_integer<int64>(ai_compose_styles_[i + 1]), ai_compose_styles_[i + 2]));
  }
  return result;
}

void TranslationManager::send_update_text_composition_styles() const {
  send_closure(G()->td(), &Td::send_update, get_update_text_composition_styles());
}

void TranslationManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }

  updates.push_back(get_update_text_composition_styles());
}

}  // namespace td
