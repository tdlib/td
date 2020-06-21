//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/VideoNotesManager.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/files/FileManager.h"
#include "td/telegram/SecretChatActor.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"

namespace td {

VideoNotesManager::VideoNotesManager(Td *td) : td_(td) {
}

int32 VideoNotesManager::get_video_note_duration(FileId file_id) const {
  auto it = video_notes_.find(file_id);
  CHECK(it != video_notes_.end());
  return it->second->duration;
}

tl_object_ptr<td_api::videoNote> VideoNotesManager::get_video_note_object(FileId file_id) {
  if (!file_id.is_valid()) {
    return nullptr;
  }

  auto &video_note = video_notes_[file_id];
  CHECK(video_note != nullptr);
  video_note->is_changed = false;

  return make_tl_object<td_api::videoNote>(
      video_note->duration, video_note->dimensions.width, get_minithumbnail_object(video_note->minithumbnail),
      get_thumbnail_object(td_->file_manager_.get(), video_note->thumbnail, PhotoFormat::Jpeg),
      td_->file_manager_->get_file_object(file_id));
}

FileId VideoNotesManager::on_get_video_note(unique_ptr<VideoNote> new_video_note, bool replace) {
  auto file_id = new_video_note->file_id;
  CHECK(file_id.is_valid());
  LOG(INFO) << "Receive video note " << file_id;
  auto &v = video_notes_[file_id];
  if (v == nullptr) {
    v = std::move(new_video_note);
  } else if (replace) {
    CHECK(v->file_id == new_video_note->file_id);
    if (v->duration != new_video_note->duration || v->dimensions != new_video_note->dimensions) {
      LOG(DEBUG) << "Video note " << file_id << " info has changed";
      v->duration = new_video_note->duration;
      v->dimensions = new_video_note->dimensions;
      v->is_changed = true;
    }
    if (v->minithumbnail != new_video_note->minithumbnail) {
      v->minithumbnail = std::move(new_video_note->minithumbnail);
      v->is_changed = true;
    }
    if (v->thumbnail != new_video_note->thumbnail) {
      if (!v->thumbnail.file_id.is_valid()) {
        LOG(DEBUG) << "Video note " << file_id << " thumbnail has changed";
      } else {
        LOG(INFO) << "Video note " << file_id << " thumbnail has changed from " << v->thumbnail << " to "
                  << new_video_note->thumbnail;
      }
      v->thumbnail = new_video_note->thumbnail;
      v->is_changed = true;
    }
  }
  return file_id;
}

const VideoNotesManager::VideoNote *VideoNotesManager::get_video_note(FileId file_id) const {
  auto video_note = video_notes_.find(file_id);
  if (video_note == video_notes_.end()) {
    return nullptr;
  }

  CHECK(video_note->second->file_id == file_id);
  return video_note->second.get();
}

FileId VideoNotesManager::get_video_note_thumbnail_file_id(FileId file_id) const {
  auto video_note = get_video_note(file_id);
  CHECK(video_note != nullptr);
  return video_note->thumbnail.file_id;
}

void VideoNotesManager::delete_video_note_thumbnail(FileId file_id) {
  auto &video_note = video_notes_[file_id];
  CHECK(video_note != nullptr);
  video_note->thumbnail = PhotoSize();
}

FileId VideoNotesManager::dup_video_note(FileId new_id, FileId old_id) {
  const VideoNote *old_video_note = get_video_note(old_id);
  CHECK(old_video_note != nullptr);
  auto &new_video_note = video_notes_[new_id];
  CHECK(!new_video_note);
  new_video_note = make_unique<VideoNote>(*old_video_note);
  new_video_note->file_id = new_id;
  new_video_note->thumbnail.file_id = td_->file_manager_->dup_file_id(new_video_note->thumbnail.file_id);
  return new_id;
}

bool VideoNotesManager::merge_video_notes(FileId new_id, FileId old_id, bool can_delete_old) {
  if (!old_id.is_valid()) {
    LOG(ERROR) << "Old file id is invalid";
    return true;
  }

  LOG(INFO) << "Merge video notes " << new_id << " and " << old_id;
  const VideoNote *old_ = get_video_note(old_id);
  CHECK(old_ != nullptr);
  if (old_id == new_id) {
    return old_->is_changed;
  }

  auto new_it = video_notes_.find(new_id);
  if (new_it == video_notes_.end()) {
    auto &old = video_notes_[old_id];
    old->is_changed = true;
    if (!can_delete_old) {
      dup_video_note(new_id, old_id);
    } else {
      old->file_id = new_id;
      video_notes_.emplace(new_id, std::move(old));
    }
  } else {
    VideoNote *new_ = new_it->second.get();
    CHECK(new_ != nullptr);
    new_->is_changed = true;
    if (old_->thumbnail != new_->thumbnail) {
      //    LOG_STATUS(td_->file_manager_->merge(new_->thumbnail.file_id, old_->thumbnail.file_id));
    }
  }
  LOG_STATUS(td_->file_manager_->merge(new_id, old_id));
  if (can_delete_old) {
    video_notes_.erase(old_id);
  }
  return true;
}

void VideoNotesManager::create_video_note(FileId file_id, string minithumbnail, PhotoSize thumbnail, int32 duration,
                                          Dimensions dimensions, bool replace) {
  auto v = make_unique<VideoNote>();
  v->file_id = file_id;
  v->duration = max(duration, 0);
  if (dimensions.width == dimensions.height && dimensions.width <= 640) {
    v->dimensions = dimensions;
  } else {
    LOG(INFO) << "Receive wrong video note dimensions " << dimensions;
  }
  v->minithumbnail = std::move(minithumbnail);
  v->thumbnail = std::move(thumbnail);
  on_get_video_note(std::move(v), replace);
}

SecretInputMedia VideoNotesManager::get_secret_input_media(FileId video_note_file_id,
                                                           tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                                           BufferSlice thumbnail, int32 layer) const {
  const VideoNote *video_note = get_video_note(video_note_file_id);
  CHECK(video_note != nullptr);
  auto file_view = td_->file_manager_->get_file_view(video_note_file_id);
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
  if (video_note->thumbnail.file_id.is_valid() && thumbnail.empty()) {
    return SecretInputMedia{};
  }
  CHECK(layer >= SecretChatActor::VIDEO_NOTES_LAYER);
  vector<tl_object_ptr<secret_api::DocumentAttribute>> attributes;
  attributes.push_back(make_tl_object<secret_api::documentAttributeVideo66>(
      secret_api::documentAttributeVideo66::ROUND_MESSAGE_MASK, true, video_note->duration,
      video_note->dimensions.width, video_note->dimensions.height));
  return SecretInputMedia{
      std::move(input_file),
      make_tl_object<secret_api::decryptedMessageMediaDocument>(
          std::move(thumbnail), video_note->thumbnail.dimensions.width, video_note->thumbnail.dimensions.height,
          "video/mp4", narrow_cast<int32>(file_view.size()), BufferSlice(encryption_key.key_slice()),
          BufferSlice(encryption_key.iv_slice()), std::move(attributes), "")};
}

tl_object_ptr<telegram_api::InputMedia> VideoNotesManager::get_input_media(
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
    const VideoNote *video_note = get_video_note(file_id);
    CHECK(video_note != nullptr);

    vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
    attributes.push_back(make_tl_object<telegram_api::documentAttributeVideo>(
        telegram_api::documentAttributeVideo::ROUND_MESSAGE_MASK, false /*ignored*/, false /*ignored*/,
        video_note->duration, video_note->dimensions.width ? video_note->dimensions.width : 240,
        video_note->dimensions.height ? video_note->dimensions.height : 240));
    int32 flags = 0;
    if (input_thumbnail != nullptr) {
      flags |= telegram_api::inputMediaUploadedDocument::THUMB_MASK;
    }
    return make_tl_object<telegram_api::inputMediaUploadedDocument>(
        flags, false /*ignored*/, false /*ignored*/, std::move(input_file), std::move(input_thumbnail), "video/mp4",
        std::move(attributes), vector<tl_object_ptr<telegram_api::InputDocument>>(), 0);
  } else {
    CHECK(!file_view.has_remote_location());
  }

  return nullptr;
}

}  // namespace td
