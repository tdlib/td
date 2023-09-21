//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/VoiceNotesManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/Dimensions.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/secret_api.h"
#include "td/telegram/Td.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"

namespace td {

VoiceNotesManager::VoiceNotesManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

VoiceNotesManager::~VoiceNotesManager() {
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), voice_notes_, voice_note_messages_,
                                              message_voice_notes_);
}

void VoiceNotesManager::tear_down() {
  parent_.reset();
}

int32 VoiceNotesManager::get_voice_note_duration(FileId file_id) const {
  auto voice_note = get_voice_note(file_id);
  if (voice_note == nullptr) {
    return 0;
  }
  return voice_note->duration;
}

tl_object_ptr<td_api::voiceNote> VoiceNotesManager::get_voice_note_object(FileId file_id) const {
  if (!file_id.is_valid()) {
    return nullptr;
  }

  auto voice_note = get_voice_note(file_id);
  CHECK(voice_note != nullptr);
  auto speech_recognition_result = voice_note->transcription_info == nullptr
                                       ? nullptr
                                       : voice_note->transcription_info->get_speech_recognition_result_object();
  return make_tl_object<td_api::voiceNote>(voice_note->duration, voice_note->waveform, voice_note->mime_type,
                                           std::move(speech_recognition_result),
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
      v->mime_type = std::move(new_voice_note->mime_type);
    }
    if (v->duration != new_voice_note->duration || v->waveform != new_voice_note->waveform) {
      LOG(DEBUG) << "Voice note " << file_id << " info has changed";
      v->duration = new_voice_note->duration;
      v->waveform = std::move(new_voice_note->waveform);
    }
    if (TranscriptionInfo::update_from(v->transcription_info, std::move(new_voice_note->transcription_info))) {
      on_voice_note_transcription_completed(file_id);
    }
  }

  return file_id;
}

VoiceNotesManager::VoiceNote *VoiceNotesManager::get_voice_note(FileId file_id) {
  return voice_notes_.get_pointer(file_id);
}

const VoiceNotesManager::VoiceNote *VoiceNotesManager::get_voice_note(FileId file_id) const {
  return voice_notes_.get_pointer(file_id);
}

FileId VoiceNotesManager::dup_voice_note(FileId new_id, FileId old_id) {
  const VoiceNote *old_voice_note = get_voice_note(old_id);
  CHECK(old_voice_note != nullptr);
  auto &new_voice_note = voice_notes_[new_id];
  CHECK(new_voice_note == nullptr);
  new_voice_note = make_unique<VoiceNote>();
  new_voice_note->file_id = new_id;
  new_voice_note->mime_type = old_voice_note->mime_type;
  new_voice_note->duration = old_voice_note->duration;
  new_voice_note->waveform = old_voice_note->waveform;
  new_voice_note->transcription_info = TranscriptionInfo::copy_if_transcribed(old_voice_note->transcription_info);
  return new_id;
}

void VoiceNotesManager::merge_voice_notes(FileId new_id, FileId old_id) {
  CHECK(old_id.is_valid() && new_id.is_valid());
  CHECK(new_id != old_id);

  LOG(INFO) << "Merge voice notes " << new_id << " and " << old_id;
  const VoiceNote *old_ = get_voice_note(old_id);
  CHECK(old_ != nullptr);

  const auto *new_ = get_voice_note(new_id);
  if (new_ == nullptr) {
    dup_voice_note(new_id, old_id);
  } else {
    if (!old_->mime_type.empty() && old_->mime_type != new_->mime_type) {
      LOG(INFO) << "Voice note has changed: mime_type = (" << old_->mime_type << ", " << new_->mime_type << ")";
    }
  }
  LOG_STATUS(td_->file_manager_->merge(new_id, old_id));
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

void VoiceNotesManager::register_voice_note(FileId voice_note_file_id, MessageFullId message_full_id,
                                            const char *source) {
  if (message_full_id.get_message_id().is_scheduled() || !message_full_id.get_message_id().is_server() ||
      td_->auth_manager_->is_bot()) {
    return;
  }
  LOG(INFO) << "Register voice note " << voice_note_file_id << " from " << message_full_id << " from " << source;
  CHECK(voice_note_file_id.is_valid());
  bool is_inserted = voice_note_messages_[voice_note_file_id].insert(message_full_id).second;
  LOG_CHECK(is_inserted) << source << ' ' << voice_note_file_id << ' ' << message_full_id;
  is_inserted = message_voice_notes_.emplace(message_full_id, voice_note_file_id).second;
  CHECK(is_inserted);
}

void VoiceNotesManager::unregister_voice_note(FileId voice_note_file_id, MessageFullId message_full_id,
                                              const char *source) {
  if (message_full_id.get_message_id().is_scheduled() || !message_full_id.get_message_id().is_server() ||
      td_->auth_manager_->is_bot()) {
    return;
  }
  LOG(INFO) << "Unregister voice note " << voice_note_file_id << " from " << message_full_id << " from " << source;
  CHECK(voice_note_file_id.is_valid());
  auto &message_ids = voice_note_messages_[voice_note_file_id];
  auto is_deleted = message_ids.erase(message_full_id) > 0;
  LOG_CHECK(is_deleted) << source << ' ' << voice_note_file_id << ' ' << message_full_id;
  if (message_ids.empty()) {
    voice_note_messages_.erase(voice_note_file_id);
  }
  is_deleted = message_voice_notes_.erase(message_full_id) > 0;
  CHECK(is_deleted);
}

void VoiceNotesManager::recognize_speech(MessageFullId message_full_id, Promise<Unit> &&promise) {
  auto it = message_voice_notes_.find(message_full_id);
  CHECK(it != message_voice_notes_.end());

  auto file_id = it->second;
  auto voice_note = get_voice_note(file_id);
  CHECK(voice_note != nullptr);
  if (voice_note->transcription_info == nullptr) {
    voice_note->transcription_info = make_unique<TranscriptionInfo>();
  }

  auto handler = [actor_id = actor_id(this),
                  file_id](Result<telegram_api::object_ptr<telegram_api::updateTranscribedAudio>> r_update) {
    send_closure(actor_id, &VoiceNotesManager::on_transcribed_audio_update, file_id, true, std::move(r_update));
  };
  if (voice_note->transcription_info->recognize_speech(td_, message_full_id, std::move(promise), std::move(handler))) {
    on_voice_note_transcription_updated(file_id);
  }
}

void VoiceNotesManager::on_transcribed_audio_update(
    FileId file_id, bool is_initial, Result<telegram_api::object_ptr<telegram_api::updateTranscribedAudio>> r_update) {
  if (G()->close_flag()) {
    return;
  }

  auto voice_note = get_voice_note(file_id);
  CHECK(voice_note != nullptr);
  CHECK(voice_note->transcription_info != nullptr);

  if (r_update.is_error()) {
    auto promises = voice_note->transcription_info->on_failed_transcription(r_update.error().clone());
    on_voice_note_transcription_updated(file_id);
    fail_promises(promises, r_update.move_as_error());
    return;
  }
  auto update = r_update.move_as_ok();
  auto transcription_id = update->transcription_id_;
  if (!update->pending_) {
    auto promises = voice_note->transcription_info->on_final_transcription(std::move(update->text_), transcription_id);
    on_voice_note_transcription_completed(file_id);
    set_promises(promises);
  } else {
    auto is_changed =
        voice_note->transcription_info->on_partial_transcription(std::move(update->text_), transcription_id);
    if (is_changed) {
      on_voice_note_transcription_updated(file_id);
    }

    if (is_initial) {
      td_->updates_manager_->subscribe_to_transcribed_audio_updates(
          transcription_id, [actor_id = actor_id(this),
                             file_id](Result<telegram_api::object_ptr<telegram_api::updateTranscribedAudio>> r_update) {
            send_closure(actor_id, &VoiceNotesManager::on_transcribed_audio_update, file_id, false,
                         std::move(r_update));
          });
    }
  }
}

void VoiceNotesManager::on_voice_note_transcription_updated(FileId file_id) {
  auto it = voice_note_messages_.find(file_id);
  if (it != voice_note_messages_.end()) {
    for (const auto &message_full_id : it->second) {
      td_->messages_manager_->on_external_update_message_content(message_full_id);
    }
  }
}

void VoiceNotesManager::on_voice_note_transcription_completed(FileId file_id) {
  auto it = voice_note_messages_.find(file_id);
  if (it != voice_note_messages_.end()) {
    for (const auto &message_full_id : it->second) {
      td_->messages_manager_->on_update_message_content(message_full_id);
    }
  }
}

void VoiceNotesManager::rate_speech_recognition(MessageFullId message_full_id, bool is_good, Promise<Unit> &&promise) {
  auto it = message_voice_notes_.find(message_full_id);
  CHECK(it != message_voice_notes_.end());

  auto file_id = it->second;
  auto voice_note = get_voice_note(file_id);
  CHECK(voice_note != nullptr);
  if (voice_note->transcription_info == nullptr) {
    return promise.set_value(Unit());
  }
  voice_note->transcription_info->rate_speech_recognition(td_, message_full_id, is_good, std::move(promise));
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
    return make_tl_object<telegram_api::inputMediaDocument>(
        0, false /*ignored*/, file_view.main_remote_location().as_input_document(), 0, string());
  }
  if (file_view.has_url()) {
    return make_tl_object<telegram_api::inputMediaDocumentExternal>(0, false /*ignored*/, file_view.url(), 0);
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
        0, false /*ignored*/, false /*ignored*/, false /*ignored*/, std::move(input_file), nullptr, mime_type,
        std::move(attributes), vector<tl_object_ptr<telegram_api::InputDocument>>(), 0);
  } else {
    CHECK(!file_view.has_remote_location());
  }

  return nullptr;
}

}  // namespace td
