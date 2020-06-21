//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/VoiceNotesManager.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/files/FileManager.h"
#include "td/telegram/Td.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"

namespace td {

VoiceNotesManager::VoiceNotesManager(Td *td) : td_(td) {
}

int32 VoiceNotesManager::get_voice_note_duration(FileId file_id) const {
  auto it = voice_notes_.find(file_id);
  CHECK(it != voice_notes_.end());
  return it->second->duration;
}

tl_object_ptr<td_api::voiceNote> VoiceNotesManager::get_voice_note_object(FileId file_id) {
  if (!file_id.is_valid()) {
    return nullptr;
  }

  auto &voice_note = voice_notes_[file_id];
  CHECK(voice_note != nullptr);
  voice_note->is_changed = false;
  return make_tl_object<td_api::voiceNote>(voice_note->duration, voice_note->waveform, voice_note->mime_type,
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
      v->is_changed = true;
    }
    if (v->duration != new_voice_note->duration || v->waveform != new_voice_note->waveform) {
      LOG(DEBUG) << "Voice note " << file_id << " info has changed";
      v->duration = new_voice_note->duration;
      v->waveform = new_voice_note->waveform;
      v->is_changed = true;
    }
  }

  return file_id;
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
  CHECK(!new_voice_note);
  new_voice_note = make_unique<VoiceNote>(*old_voice_note);
  new_voice_note->file_id = new_id;
  return new_id;
}

bool VoiceNotesManager::merge_voice_notes(FileId new_id, FileId old_id, bool can_delete_old) {
  if (!old_id.is_valid()) {
    LOG(ERROR) << "Old file id is invalid";
    return true;
  }

  LOG(INFO) << "Merge voice notes " << new_id << " and " << old_id;
  const VoiceNote *old_ = get_voice_note(old_id);
  CHECK(old_ != nullptr);
  if (old_id == new_id) {
    return old_->is_changed;
  }

  auto new_it = voice_notes_.find(new_id);
  if (new_it == voice_notes_.end()) {
    auto &old = voice_notes_[old_id];
    old->is_changed = true;
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

    new_->is_changed = true;
  }
  LOG_STATUS(td_->file_manager_->merge(new_id, old_id));
  if (can_delete_old) {
    voice_notes_.erase(old_id);
  }
  return true;
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

SecretInputMedia VoiceNotesManager::get_secret_input_media(FileId voice_file_id,
                                                           tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                                           const string &caption) const {
  auto *voice_note = get_voice_note(voice_file_id);
  CHECK(voice_note != nullptr);
  auto file_view = td_->file_manager_->get_file_view(voice_file_id);
  auto &encryption_key = file_view.encryption_key();
  if (!file_view.is_encrypted_secret() || encryption_key.empty()) {
    return SecretInputMedia{};
  }
  if (file_view.has_remote_location()) {
    input_file = file_view.main_remote_location().as_input_encrypted_file();
  }
  if (!input_file) {
    return SecretInputMedia{};
  }
  vector<tl_object_ptr<secret_api::DocumentAttribute>> attributes;
  attributes.push_back(make_tl_object<secret_api::documentAttributeAudio>(
      secret_api::documentAttributeAudio::VOICE_MASK | secret_api::documentAttributeAudio::WAVEFORM_MASK,
      false /*ignored*/, voice_note->duration, "", "", BufferSlice(voice_note->waveform)));
  return SecretInputMedia{std::move(input_file),
                          make_tl_object<secret_api::decryptedMessageMediaDocument>(
                              BufferSlice(), 0, 0, voice_note->mime_type, narrow_cast<int32>(file_view.size()),
                              BufferSlice(encryption_key.key_slice()), BufferSlice(encryption_key.iv_slice()),
                              std::move(attributes), caption)};
}

tl_object_ptr<telegram_api::InputMedia> VoiceNotesManager::get_input_media(
    FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) const {
  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.is_encrypted()) {
    return nullptr;
  }
  if (file_view.has_remote_location() && !file_view.main_remote_location().is_web() && input_file == nullptr) {
    return make_tl_object<telegram_api::inputMediaDocument>(0, file_view.main_remote_location().as_input_document(), 0);
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
