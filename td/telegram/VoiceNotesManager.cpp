//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/VoiceNotesManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/Dimensions.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/secret_api.h"
#include "td/telegram/Td.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"

namespace td {

class TranscribeAudioQuery final : public Td::ResultHandler {
  DialogId dialog_id_;
  FileId file_id_;

 public:
  void send(FileId file_id, FullMessageId full_message_id) {
    dialog_id_ = full_message_id.get_dialog_id();
    file_id_ = file_id;
    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_transcribeAudio(
        std::move(input_peer), full_message_id.get_message_id().get_server_message_id().get())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_transcribeAudio>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for TranscribeAudioQuery: " << to_string(result);
    if (result->transcription_id_ == 0) {
      return on_error(Status::Error(500, "Receive no recognition identifier"));
    }
    td_->voice_notes_manager_->on_voice_note_transcribed(file_id_, std::move(result->text_), result->transcription_id_,
                                                         !result->pending_);
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "TranscribeAudioQuery");
    td_->voice_notes_manager_->on_voice_note_transcription_failed(file_id_, std::move(status));
  }
};

class RateTranscribedAudioQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit RateTranscribedAudioQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(FullMessageId full_message_id, int64 transcription_id, bool is_good) {
    dialog_id_ = full_message_id.get_dialog_id();
    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_rateTranscribedAudio(
        std::move(input_peer), full_message_id.get_message_id().get_server_message_id().get(), transcription_id,
        is_good)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_rateTranscribedAudio>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    LOG(INFO) << "Receive result for RateTranscribedAudioQuery: " << result;
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "RateTranscribedAudioQuery");
    promise_.set_error(std::move(status));
  }
};

VoiceNotesManager::VoiceNotesManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  voice_note_transcription_timeout_.set_callback(on_voice_note_transcription_timeout_callback);
  voice_note_transcription_timeout_.set_callback_data(static_cast<void *>(this));
}

void VoiceNotesManager::tear_down() {
  parent_.reset();
}

void VoiceNotesManager::on_voice_note_transcription_timeout_callback(void *voice_notes_manager_ptr,
                                                                     int64 transcription_id) {
  if (G()->close_flag()) {
    return;
  }

  auto voice_notes_manager = static_cast<VoiceNotesManager *>(voice_notes_manager_ptr);
  send_closure_later(voice_notes_manager->actor_id(voice_notes_manager),
                     &VoiceNotesManager::on_pending_voice_note_transcription_failed, transcription_id,
                     Status::Error(500, "Timeout expired"));
}

int32 VoiceNotesManager::get_voice_note_duration(FileId file_id) const {
  auto it = voice_notes_.find(file_id);
  if (it == voice_notes_.end()) {
    return 0;
  }
  return it->second->duration;
}

tl_object_ptr<td_api::voiceNote> VoiceNotesManager::get_voice_note_object(FileId file_id) const {
  if (!file_id.is_valid()) {
    return nullptr;
  }

  auto it = voice_notes_.find(file_id);
  CHECK(it != voice_notes_.end());
  auto voice_note = it->second.get();
  CHECK(voice_note != nullptr);
  return make_tl_object<td_api::voiceNote>(voice_note->duration, voice_note->waveform, voice_note->mime_type,
                                           voice_note->is_transcribed, voice_note->text,
                                           td_->file_manager_->get_file_object(file_id));
}

FileId VoiceNotesManager::on_get_voice_note(unique_ptr<VoiceNote> new_voice_note, bool replace) {
  auto file_id = new_voice_note->file_id;
  CHECK(file_id.is_valid());
  LOG(INFO) << "Receive voice note " << file_id;
  auto &v = voice_notes_[file_id];
  if (v == nullptr) {
    v = std::move(new_voice_note);
  } else if (replace) {
    CHECK(v->file_id == new_voice_note->file_id);
    if (v->mime_type != new_voice_note->mime_type) {
      LOG(DEBUG) << "Voice note " << file_id << " info has changed";
      v->mime_type = new_voice_note->mime_type;
    }
    if (v->duration != new_voice_note->duration || v->waveform != new_voice_note->waveform) {
      LOG(DEBUG) << "Voice note " << file_id << " info has changed";
      v->duration = new_voice_note->duration;
      v->waveform = new_voice_note->waveform;
    }
    if (new_voice_note->is_transcribed && v->transcription_id == 0) {
      CHECK(!v->is_transcribed);
      CHECK(new_voice_note->transcription_id != 0);
      v->is_transcribed = true;
      v->transcription_id = new_voice_note->transcription_id;
      v->text = std::move(new_voice_note->text);
      on_voice_note_transcription_updated(file_id);
    }
  }

  return file_id;
}

VoiceNotesManager::VoiceNote *VoiceNotesManager::get_voice_note(FileId file_id) {
  auto voice_note = voice_notes_.find(file_id);
  if (voice_note == voice_notes_.end()) {
    return nullptr;
  }

  CHECK(voice_note->second->file_id == file_id);
  return voice_note->second.get();
}

const VoiceNotesManager::VoiceNote *VoiceNotesManager::get_voice_note(FileId file_id) const {
  auto voice_note = voice_notes_.find(file_id);
  if (voice_note == voice_notes_.end()) {
    return nullptr;
  }

  CHECK(voice_note->second->file_id == file_id);
  return voice_note->second.get();
}

FileId VoiceNotesManager::dup_voice_note(FileId new_id, FileId old_id) {
  const VoiceNote *old_voice_note = get_voice_note(old_id);
  CHECK(old_voice_note != nullptr);
  auto &new_voice_note = voice_notes_[new_id];
  CHECK(new_voice_note == nullptr);
  new_voice_note = make_unique<VoiceNote>(*old_voice_note);
  new_voice_note->file_id = new_id;
  return new_id;
}

void VoiceNotesManager::merge_voice_notes(FileId new_id, FileId old_id, bool can_delete_old) {
  CHECK(old_id.is_valid() && new_id.is_valid());
  CHECK(new_id != old_id);

  LOG(INFO) << "Merge voice notes " << new_id << " and " << old_id;
  const VoiceNote *old_ = get_voice_note(old_id);
  CHECK(old_ != nullptr);

  auto new_it = voice_notes_.find(new_id);
  if (new_it == voice_notes_.end()) {
    auto &old = voice_notes_[old_id];
    if (!can_delete_old) {
      dup_voice_note(new_id, old_id);
    } else {
      old->file_id = new_id;
      voice_notes_.emplace(new_id, std::move(old));
    }
  } else {
    VoiceNote *new_ = new_it->second.get();
    CHECK(new_ != nullptr);

    if (!old_->mime_type.empty() && old_->mime_type != new_->mime_type) {
      LOG(INFO) << "Voice note has changed: mime_type = (" << old_->mime_type << ", " << new_->mime_type << ")";
    }
  }
  LOG_STATUS(td_->file_manager_->merge(new_id, old_id));
  if (can_delete_old) {
    voice_notes_.erase(old_id);
  }
}

void VoiceNotesManager::create_voice_note(FileId file_id, string mime_type, int32 duration, string waveform,
                                          bool replace) {
  auto v = make_unique<VoiceNote>();
  v->file_id = file_id;
  v->mime_type = std::move(mime_type);
  v->duration = max(duration, 0);
  v->waveform = std::move(waveform);
  on_get_voice_note(std::move(v), replace);
}

void VoiceNotesManager::register_voice_note(FileId voice_note_file_id, FullMessageId full_message_id,
                                            const char *source) {
  if (full_message_id.get_message_id().is_scheduled() || !full_message_id.get_message_id().is_server()) {
    return;
  }
  LOG(INFO) << "Register voice note " << voice_note_file_id << " from " << full_message_id << " from " << source;
  bool is_inserted = voice_note_messages_[voice_note_file_id].insert(full_message_id).second;
  LOG_CHECK(is_inserted) << source << ' ' << voice_note_file_id << ' ' << full_message_id;
  is_inserted = message_voice_notes_.emplace(full_message_id, voice_note_file_id).second;
  CHECK(is_inserted);
}

void VoiceNotesManager::unregister_voice_note(FileId voice_note_file_id, FullMessageId full_message_id,
                                              const char *source) {
  if (full_message_id.get_message_id().is_scheduled() || !full_message_id.get_message_id().is_server()) {
    return;
  }
  LOG(INFO) << "Unregister voice note " << voice_note_file_id << " from " << full_message_id << " from " << source;
  auto &message_ids = voice_note_messages_[voice_note_file_id];
  auto is_deleted = message_ids.erase(full_message_id) > 0;
  LOG_CHECK(is_deleted) << source << ' ' << voice_note_file_id << ' ' << full_message_id;
  if (message_ids.empty()) {
    voice_note_messages_.erase(voice_note_file_id);
  }
  is_deleted = message_voice_notes_.erase(full_message_id) > 0;
  CHECK(is_deleted);
}

void VoiceNotesManager::recognize_speech(FullMessageId full_message_id, Promise<Unit> &&promise) {
  if (!td_->messages_manager_->have_message_force(full_message_id, "recognize_speech")) {
    return promise.set_error(Status::Error(400, "Message not found"));
  }

  auto it = message_voice_notes_.find(full_message_id);
  if (it == message_voice_notes_.end()) {
    return promise.set_error(Status::Error(400, "Invalid message specified"));
  }

  auto file_id = it->second;
  auto voice_note = get_voice_note(file_id);
  CHECK(voice_note != nullptr);
  if (voice_note->is_transcribed) {
    return promise.set_value(Unit());
  }
  auto &queries = speech_recognition_queries_[file_id];
  queries.push_back(std::move(promise));
  if (queries.size() == 1) {
    td_->create_handler<TranscribeAudioQuery>()->send(file_id, full_message_id);
  }
}

void VoiceNotesManager::on_voice_note_transcribed(FileId file_id, string &&text, int64 transcription_id,
                                                  bool is_final) {
  auto voice_note = get_voice_note(file_id);
  CHECK(voice_note != nullptr);
  CHECK(!voice_note->is_transcribed);
  CHECK(voice_note->transcription_id == 0 || voice_note->transcription_id == transcription_id);
  bool is_changed = voice_note->is_transcribed != is_final || voice_note->text != text;
  voice_note->transcription_id = transcription_id;
  voice_note->is_transcribed = is_final;
  voice_note->text = std::move(text);

  if (is_changed) {
    on_voice_note_transcription_updated(file_id);
  }

  if (is_final) {
    auto it = speech_recognition_queries_.find(file_id);
    CHECK(it != speech_recognition_queries_.end());
    CHECK(!it->second.empty());
    auto promises = std::move(it->second);
    speech_recognition_queries_.erase(it);

    set_promises(promises);
  } else {
    if (pending_voice_note_transcription_queries_.count(transcription_id) != 0) {
      on_pending_voice_note_transcription_failed(transcription_id,
                                                 Status::Error(500, "Receive duplicate recognition identifier"));
    }
    bool is_inserted = pending_voice_note_transcription_queries_.emplace(transcription_id, file_id).second;
    CHECK(is_inserted);
    voice_note_transcription_timeout_.set_timeout_in(transcription_id, TRANSCRIPTION_TIMEOUT);
  }
}

void VoiceNotesManager::on_voice_note_transcription_failed(FileId file_id, Status &&error) {
  auto voice_note = get_voice_note(file_id);
  CHECK(voice_note != nullptr);
  CHECK(!voice_note->is_transcribed);

  if (voice_note->transcription_id != 0) {
    CHECK(pending_voice_note_transcription_queries_.count(voice_note->transcription_id) == 0);
    voice_note->transcription_id = 0;
    if (!voice_note->text.empty()) {
      voice_note->text.clear();
      on_voice_note_transcription_updated(file_id);
    }
  }

  auto it = speech_recognition_queries_.find(file_id);
  CHECK(it != speech_recognition_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  speech_recognition_queries_.erase(it);

  fail_promises(promises, std::move(error));
}

void VoiceNotesManager::on_update_transcribed_audio(string &&text, int64 transcription_id, bool is_final) {
  auto it = pending_voice_note_transcription_queries_.find(transcription_id);
  if (it == pending_voice_note_transcription_queries_.end()) {
    return;
  }
  auto file_id = it->second;
  pending_voice_note_transcription_queries_.erase(it);
  voice_note_transcription_timeout_.cancel_timeout(transcription_id);

  on_voice_note_transcribed(file_id, std::move(text), transcription_id, is_final);
}

void VoiceNotesManager::on_pending_voice_note_transcription_failed(int64 transcription_id, Status &&error) {
  auto it = pending_voice_note_transcription_queries_.find(transcription_id);
  if (it == pending_voice_note_transcription_queries_.end()) {
    return;
  }
  auto file_id = it->second;
  pending_voice_note_transcription_queries_.erase(it);
  voice_note_transcription_timeout_.cancel_timeout(transcription_id);

  on_voice_note_transcription_failed(file_id, std::move(error));
}

void VoiceNotesManager::on_voice_note_transcription_updated(FileId file_id) {
  auto it = voice_note_messages_.find(file_id);
  if (it != voice_note_messages_.end()) {
    for (const auto &full_message_id : it->second) {
      td_->messages_manager_->on_external_update_message_content(full_message_id);
    }
  }
}

void VoiceNotesManager::rate_speech_recognition(FullMessageId full_message_id, bool is_good, Promise<Unit> &&promise) {
  if (!td_->messages_manager_->have_message_force(full_message_id, "rate_speech_recognition")) {
    return promise.set_error(Status::Error(400, "Message not found"));
  }

  auto it = message_voice_notes_.find(full_message_id);
  if (it == message_voice_notes_.end()) {
    return promise.set_error(Status::Error(400, "Invalid message specified"));
  }

  auto file_id = it->second;
  auto voice_note = get_voice_note(file_id);
  CHECK(voice_note != nullptr);
  if (!voice_note->is_transcribed) {
    return promise.set_value(Unit());
  }
  CHECK(voice_note->transcription_id != 0);
  td_->create_handler<RateTranscribedAudioQuery>(std::move(promise))
      ->send(full_message_id, voice_note->transcription_id, is_good);
}

SecretInputMedia VoiceNotesManager::get_secret_input_media(FileId voice_note_file_id,
                                                           tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                                           const string &caption, int32 layer) const {
  auto file_view = td_->file_manager_->get_file_view(voice_note_file_id);
  if (!file_view.is_encrypted_secret() || file_view.encryption_key().empty()) {
    return SecretInputMedia{};
  }
  if (file_view.has_remote_location()) {
    input_file = file_view.main_remote_location().as_input_encrypted_file();
  }
  if (!input_file) {
    return SecretInputMedia{};
  }

  auto *voice_note = get_voice_note(voice_note_file_id);
  CHECK(voice_note != nullptr);
  vector<tl_object_ptr<secret_api::DocumentAttribute>> attributes;
  attributes.push_back(make_tl_object<secret_api::documentAttributeAudio>(
      secret_api::documentAttributeAudio::VOICE_MASK | secret_api::documentAttributeAudio::WAVEFORM_MASK,
      false /*ignored*/, voice_note->duration, "", "", BufferSlice(voice_note->waveform)));

  return {std::move(input_file), BufferSlice(), Dimensions(), voice_note->mime_type, file_view,
          std::move(attributes), caption,       layer};
}

tl_object_ptr<telegram_api::InputMedia> VoiceNotesManager::get_input_media(
    FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) const {
  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.is_encrypted()) {
    return nullptr;
  }
  if (file_view.has_remote_location() && !file_view.main_remote_location().is_web() && input_file == nullptr) {
    return make_tl_object<telegram_api::inputMediaDocument>(0, file_view.main_remote_location().as_input_document(), 0,
                                                            string());
  }
  if (file_view.has_url()) {
    return make_tl_object<telegram_api::inputMediaDocumentExternal>(0, file_view.url(), 0);
  }

  if (input_file != nullptr) {
    const VoiceNote *voice_note = get_voice_note(file_id);
    CHECK(voice_note != nullptr);

    vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
    int32 flags = telegram_api::documentAttributeAudio::VOICE_MASK;
    if (!voice_note->waveform.empty()) {
      flags |= telegram_api::documentAttributeAudio::WAVEFORM_MASK;
    }
    attributes.push_back(make_tl_object<telegram_api::documentAttributeAudio>(
        flags, false /*ignored*/, voice_note->duration, "", "", BufferSlice(voice_note->waveform)));
    string mime_type = voice_note->mime_type;
    if (mime_type != "audio/ogg" && mime_type != "audio/mpeg" && mime_type != "audio/mp4") {
      mime_type = "audio/ogg";
    }
    return make_tl_object<telegram_api::inputMediaUploadedDocument>(
        0, false /*ignored*/, false /*ignored*/, std::move(input_file), nullptr, mime_type, std::move(attributes),
        vector<tl_object_ptr<telegram_api::InputDocument>>(), 0);
  } else {
    CHECK(!file_view.has_remote_location());
  }

  return nullptr;
}

}  // namespace td
