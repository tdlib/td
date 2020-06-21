//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AudiosManager.h"

#include "td/telegram/files/FileManager.h"
#include "td/telegram/Td.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"

namespace td {

AudiosManager::AudiosManager(Td *td) : td_(td) {
}

int32 AudiosManager::get_audio_duration(FileId file_id) const {
  auto it = audios_.find(file_id);
  CHECK(it != audios_.end());
  return it->second->duration;
}

tl_object_ptr<td_api::audio> AudiosManager::get_audio_object(FileId file_id) {
  if (!file_id.is_valid()) {
    return nullptr;
  }

  auto &audio = audios_[file_id];
  CHECK(audio != nullptr);
  audio->is_changed = false;
  return make_tl_object<td_api::audio>(
      audio->duration, audio->title, audio->performer, audio->file_name, audio->mime_type,
      get_minithumbnail_object(audio->minithumbnail),
      get_thumbnail_object(td_->file_manager_.get(), audio->thumbnail, PhotoFormat::Jpeg),
      td_->file_manager_->get_file_object(file_id));
}

FileId AudiosManager::on_get_audio(unique_ptr<Audio> new_audio, bool replace) {
  auto file_id = new_audio->file_id;
  CHECK(file_id.is_valid());
  LOG(INFO) << "Receive audio " << file_id;
  auto &a = audios_[file_id];
  if (a == nullptr) {
    a = std::move(new_audio);
  } else if (replace) {
    CHECK(a->file_id == new_audio->file_id);
    if (a->mime_type != new_audio->mime_type) {
      LOG(DEBUG) << "Audio " << file_id << " info has changed";
      a->mime_type = new_audio->mime_type;
      a->is_changed = true;
    }
    if (a->duration != new_audio->duration || a->title != new_audio->title || a->performer != new_audio->performer) {
      LOG(DEBUG) << "Audio " << file_id << " info has changed";
      a->duration = new_audio->duration;
      a->title = new_audio->title;
      a->performer = new_audio->performer;
      a->is_changed = true;
    }
    if (a->file_name != new_audio->file_name) {
      LOG(DEBUG) << "Audio " << file_id << " file name has changed";
      a->file_name = std::move(new_audio->file_name);
      a->is_changed = true;
    }
    if (a->minithumbnail != new_audio->minithumbnail) {
      a->minithumbnail = std::move(new_audio->minithumbnail);
      a->is_changed = true;
    }
    if (a->thumbnail != new_audio->thumbnail) {
      if (!a->thumbnail.file_id.is_valid()) {
        LOG(DEBUG) << "Audio " << file_id << " thumbnail has changed";
      } else {
        LOG(INFO) << "Audio " << file_id << " thumbnail has changed from " << a->thumbnail << " to "
                  << new_audio->thumbnail;
      }
      a->thumbnail = new_audio->thumbnail;
      a->is_changed = true;
    }
  }

  return file_id;
}

const AudiosManager::Audio *AudiosManager::get_audio(FileId file_id) const {
  auto audio = audios_.find(file_id);
  if (audio == audios_.end()) {
    return nullptr;
  }

  CHECK(audio->second->file_id == file_id);
  return audio->second.get();
}

FileId AudiosManager::dup_audio(FileId new_id, FileId old_id) {
  const Audio *old_audio = get_audio(old_id);
  CHECK(old_audio != nullptr);
  auto &new_audio = audios_[new_id];
  CHECK(!new_audio);
  new_audio = make_unique<Audio>(*old_audio);
  new_audio->file_id = new_id;
  new_audio->thumbnail.file_id = td_->file_manager_->dup_file_id(new_audio->thumbnail.file_id);
  return new_id;
}

bool AudiosManager::merge_audios(FileId new_id, FileId old_id, bool can_delete_old) {
  if (!old_id.is_valid()) {
    LOG(ERROR) << "Old file id is invalid";
    return true;
  }

  LOG(INFO) << "Merge audios " << new_id << " and " << old_id;
  const Audio *old_ = get_audio(old_id);
  CHECK(old_ != nullptr);
  if (old_id == new_id) {
    return old_->is_changed;
  }

  auto new_it = audios_.find(new_id);
  if (new_it == audios_.end()) {
    auto &old = audios_[old_id];
    old->is_changed = true;
    if (!can_delete_old) {
      dup_audio(new_id, old_id);
    } else {
      old->file_id = new_id;
      audios_.emplace(new_id, std::move(old));
    }
  } else {
    Audio *new_ = new_it->second.get();
    CHECK(new_ != nullptr);

    if (!old_->mime_type.empty() && old_->mime_type != new_->mime_type) {
      LOG(INFO) << "Audio has changed: mime_type = (" << old_->mime_type << ", " << new_->mime_type << ")";
    }

    new_->is_changed = true;
    if (old_->thumbnail != new_->thumbnail) {
      //    LOG_STATUS(td_->file_manager_->merge(new_->thumbnail.file_id, old_->thumbnail.file_id));
    }
  }
  LOG_STATUS(td_->file_manager_->merge(new_id, old_id));
  if (can_delete_old) {
    audios_.erase(old_id);
  }
  return true;
}

string AudiosManager::get_audio_search_text(FileId file_id) const {
  auto audio = get_audio(file_id);
  CHECK(audio != nullptr);
  return PSTRING() << audio->file_name << " " << audio->title << " " << audio->performer;
}

FileId AudiosManager::get_audio_thumbnail_file_id(FileId file_id) const {
  auto audio = get_audio(file_id);
  CHECK(audio != nullptr);
  return audio->thumbnail.file_id;
}

void AudiosManager::delete_audio_thumbnail(FileId file_id) {
  auto &audio = audios_[file_id];
  CHECK(audio != nullptr);
  audio->thumbnail = PhotoSize();
}

void AudiosManager::create_audio(FileId file_id, string minithumbnail, PhotoSize thumbnail, string file_name,
                                 string mime_type, int32 duration, string title, string performer, bool replace) {
  auto a = make_unique<Audio>();
  a->file_id = file_id;
  a->file_name = std::move(file_name);
  a->mime_type = std::move(mime_type);
  a->duration = max(duration, 0);
  a->title = std::move(title);
  a->performer = std::move(performer);
  a->minithumbnail = std::move(minithumbnail);
  a->thumbnail = std::move(thumbnail);
  on_get_audio(std::move(a), replace);
}

SecretInputMedia AudiosManager::get_secret_input_media(FileId audio_file_id,
                                                       tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                                       const string &caption, BufferSlice thumbnail) const {
  auto *audio = get_audio(audio_file_id);
  CHECK(audio != nullptr);
  auto file_view = td_->file_manager_->get_file_view(audio_file_id);
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
  if (audio->thumbnail.file_id.is_valid() && thumbnail.empty()) {
    return SecretInputMedia{};
  }
  vector<tl_object_ptr<secret_api::DocumentAttribute>> attributes;
  if (!audio->file_name.empty()) {
    attributes.push_back(make_tl_object<secret_api::documentAttributeFilename>(audio->file_name));
  }
  attributes.push_back(make_tl_object<secret_api::documentAttributeAudio>(
      secret_api::documentAttributeAudio::TITLE_MASK | secret_api::documentAttributeAudio::PERFORMER_MASK,
      false /*ignored*/, audio->duration, audio->title, audio->performer, BufferSlice()));

  return SecretInputMedia{
      std::move(input_file),
      make_tl_object<secret_api::decryptedMessageMediaDocument>(
          std::move(thumbnail), audio->thumbnail.dimensions.width, audio->thumbnail.dimensions.height, audio->mime_type,
          narrow_cast<int32>(file_view.size()), BufferSlice(encryption_key.key_slice()),
          BufferSlice(encryption_key.iv_slice()), std::move(attributes), caption)};
}

tl_object_ptr<telegram_api::InputMedia> AudiosManager::get_input_media(
    FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file,
    tl_object_ptr<telegram_api::InputFile> input_thumbnail) const {
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
    const Audio *audio = get_audio(file_id);
    CHECK(audio != nullptr);

    vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
    attributes.push_back(make_tl_object<telegram_api::documentAttributeAudio>(
        telegram_api::documentAttributeAudio::TITLE_MASK | telegram_api::documentAttributeAudio::PERFORMER_MASK,
        false /*ignored*/, audio->duration, audio->title, audio->performer, BufferSlice()));
    if (!audio->file_name.empty()) {
      attributes.push_back(make_tl_object<telegram_api::documentAttributeFilename>(audio->file_name));
    }
    string mime_type = audio->mime_type;
    if (!begins_with(mime_type, "audio/")) {
      mime_type = "audio/mpeg";
    }
    int32 flags = 0;
    if (input_thumbnail != nullptr) {
      flags |= telegram_api::inputMediaUploadedDocument::THUMB_MASK;
    }
    return make_tl_object<telegram_api::inputMediaUploadedDocument>(
        flags, false /*ignored*/, false /*ignored*/, std::move(input_file), std::move(input_thumbnail), mime_type,
        std::move(attributes), vector<tl_object_ptr<telegram_api::InputDocument>>(), 0);
  } else {
    CHECK(!file_view.has_remote_location());
  }

  return nullptr;
}

}  // namespace td
