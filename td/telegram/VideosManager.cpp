//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/VideosManager.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/files/FileManager.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"

namespace td {

VideosManager::VideosManager(Td *td) : td_(td) {
}

int32 VideosManager::get_video_duration(FileId file_id) const {
  auto it = videos_.find(file_id);
  CHECK(it != videos_.end());
  return it->second->duration;
}

tl_object_ptr<td_api::video> VideosManager::get_video_object(FileId file_id) {
  if (!file_id.is_valid()) {
    return nullptr;
  }

  auto &video = videos_[file_id];
  CHECK(video != nullptr);
  video->is_changed = false;

  auto thumbnail = video->animated_thumbnail.file_id.is_valid()
                       ? get_thumbnail_object(td_->file_manager_.get(), video->animated_thumbnail, PhotoFormat::Mpeg4)
                       : get_thumbnail_object(td_->file_manager_.get(), video->thumbnail, PhotoFormat::Jpeg);
  return make_tl_object<td_api::video>(video->duration, video->dimensions.width, video->dimensions.height,
                                       video->file_name, video->mime_type, video->has_stickers,
                                       video->supports_streaming, get_minithumbnail_object(video->minithumbnail),
                                       std::move(thumbnail), td_->file_manager_->get_file_object(file_id));
}

FileId VideosManager::on_get_video(unique_ptr<Video> new_video, bool replace) {
  auto file_id = new_video->file_id;
  CHECK(file_id.is_valid());
  LOG(INFO) << "Receive video " << file_id;
  auto &v = videos_[file_id];
  if (v == nullptr) {
    v = std::move(new_video);
  } else if (replace) {
    CHECK(v->file_id == new_video->file_id);
    if (v->mime_type != new_video->mime_type) {
      LOG(DEBUG) << "Video " << file_id << " MIME type has changed";
      v->mime_type = new_video->mime_type;
      v->is_changed = true;
    }
    if (v->duration != new_video->duration || v->dimensions != new_video->dimensions ||
        v->supports_streaming != new_video->supports_streaming) {
      LOG(DEBUG) << "Video " << file_id << " info has changed";
      v->duration = new_video->duration;
      v->dimensions = new_video->dimensions;
      v->supports_streaming = new_video->supports_streaming;
      v->is_changed = true;
    }
    if (v->file_name != new_video->file_name) {
      LOG(DEBUG) << "Video " << file_id << " file name has changed";
      v->file_name = std::move(new_video->file_name);
      v->is_changed = true;
    }
    if (v->minithumbnail != new_video->minithumbnail) {
      v->minithumbnail = std::move(new_video->minithumbnail);
      v->is_changed = true;
    }
    if (v->thumbnail != new_video->thumbnail) {
      if (!v->thumbnail.file_id.is_valid()) {
        LOG(DEBUG) << "Video " << file_id << " thumbnail has changed";
      } else {
        LOG(INFO) << "Video " << file_id << " thumbnail has changed from " << v->thumbnail << " to "
                  << new_video->thumbnail;
      }
      v->thumbnail = new_video->thumbnail;
      v->is_changed = true;
    }
    if (v->animated_thumbnail != new_video->animated_thumbnail) {
      if (!v->animated_thumbnail.file_id.is_valid()) {
        LOG(DEBUG) << "Video " << file_id << " animated thumbnail has changed";
      } else {
        LOG(INFO) << "Video " << file_id << " animated thumbnail has changed from " << v->animated_thumbnail << " to "
                  << new_video->animated_thumbnail;
      }
      v->animated_thumbnail = new_video->animated_thumbnail;
      v->is_changed = true;
    }
    if (v->has_stickers != new_video->has_stickers && new_video->has_stickers) {
      v->has_stickers = new_video->has_stickers;
      v->is_changed = true;
    }
    if (v->sticker_file_ids != new_video->sticker_file_ids && !new_video->sticker_file_ids.empty()) {
      v->sticker_file_ids = std::move(new_video->sticker_file_ids);
      v->is_changed = true;
    }
  }
  return file_id;
}

const VideosManager::Video *VideosManager::get_video(FileId file_id) const {
  auto video = videos_.find(file_id);
  if (video == videos_.end()) {
    return nullptr;
  }

  CHECK(video->second->file_id == file_id);
  return video->second.get();
}

FileId VideosManager::get_video_thumbnail_file_id(FileId file_id) const {
  auto video = get_video(file_id);
  CHECK(video != nullptr);
  return video->thumbnail.file_id;
}

FileId VideosManager::get_video_animated_thumbnail_file_id(FileId file_id) const {
  auto video = get_video(file_id);
  CHECK(video != nullptr);
  return video->animated_thumbnail.file_id;
}

void VideosManager::delete_video_thumbnail(FileId file_id) {
  auto &video = videos_[file_id];
  CHECK(video != nullptr);
  video->thumbnail = PhotoSize();
  video->animated_thumbnail = AnimationSize();
}

FileId VideosManager::dup_video(FileId new_id, FileId old_id) {
  const Video *old_video = get_video(old_id);
  CHECK(old_video != nullptr);
  auto &new_video = videos_[new_id];
  CHECK(!new_video);
  new_video = make_unique<Video>(*old_video);
  new_video->file_id = new_id;
  new_video->thumbnail.file_id = td_->file_manager_->dup_file_id(new_video->thumbnail.file_id);
  new_video->animated_thumbnail.file_id = td_->file_manager_->dup_file_id(new_video->animated_thumbnail.file_id);
  return new_id;
}

bool VideosManager::merge_videos(FileId new_id, FileId old_id, bool can_delete_old) {
  if (!old_id.is_valid()) {
    LOG(ERROR) << "Old file id is invalid";
    return true;
  }

  LOG(INFO) << "Merge videos " << new_id << " and " << old_id;
  const Video *old_ = get_video(old_id);
  CHECK(old_ != nullptr);
  if (old_id == new_id) {
    return old_->is_changed;
  }

  auto new_it = videos_.find(new_id);
  if (new_it == videos_.end()) {
    auto &old = videos_[old_id];
    old->is_changed = true;
    if (!can_delete_old) {
      dup_video(new_id, old_id);
    } else {
      old->file_id = new_id;
      videos_.emplace(new_id, std::move(old));
    }
  } else {
    Video *new_ = new_it->second.get();
    CHECK(new_ != nullptr);

    if (!old_->mime_type.empty() && old_->mime_type != new_->mime_type) {
      LOG(INFO) << "Video has changed: mime_type = (" << old_->mime_type << ", " << new_->mime_type << ")";
    }

    new_->is_changed = true;
    if (old_->thumbnail != new_->thumbnail) {
      //    LOG_STATUS(td_->file_manager_->merge(new_->thumbnail.file_id, old_->thumbnail.file_id));
    }
  }
  LOG_STATUS(td_->file_manager_->merge(new_id, old_id));
  if (can_delete_old) {
    videos_.erase(old_id);
  }
  return true;
}

void VideosManager::create_video(FileId file_id, string minithumbnail, PhotoSize thumbnail,
                                 AnimationSize animated_thumbnail, bool has_stickers, vector<FileId> &&sticker_file_ids,
                                 string file_name, string mime_type, int32 duration, Dimensions dimensions,
                                 bool supports_streaming, bool replace) {
  auto v = make_unique<Video>();
  v->file_id = file_id;
  v->file_name = std::move(file_name);
  v->mime_type = std::move(mime_type);
  v->duration = max(duration, 0);
  v->dimensions = dimensions;
  v->minithumbnail = std::move(minithumbnail);
  v->thumbnail = std::move(thumbnail);
  v->animated_thumbnail = std::move(animated_thumbnail);
  v->supports_streaming = supports_streaming;
  v->has_stickers = has_stickers;
  v->sticker_file_ids = std::move(sticker_file_ids);
  on_get_video(std::move(v), replace);
}

SecretInputMedia VideosManager::get_secret_input_media(FileId video_file_id,
                                                       tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                                       const string &caption, BufferSlice thumbnail) const {
  const Video *video = get_video(video_file_id);
  CHECK(video != nullptr);
  auto file_view = td_->file_manager_->get_file_view(video_file_id);
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
  if (video->thumbnail.file_id.is_valid() && thumbnail.empty()) {
    return {};
  }
  return SecretInputMedia{
      std::move(input_file),
      make_tl_object<secret_api::decryptedMessageMediaVideo>(
          std::move(thumbnail), video->thumbnail.dimensions.width, video->thumbnail.dimensions.height, video->duration,
          video->mime_type, video->dimensions.width, video->dimensions.height, narrow_cast<int32>(file_view.size()),
          BufferSlice(encryption_key.key_slice()), BufferSlice(encryption_key.iv_slice()), caption)};
}

tl_object_ptr<telegram_api::InputMedia> VideosManager::get_input_media(
    FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file,
    tl_object_ptr<telegram_api::InputFile> input_thumbnail, int32 ttl) const {
  if (!file_id.is_valid()) {
    LOG_IF(ERROR, ttl == 0) << "Video has invalid file_id";
    return nullptr;
  }
  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.is_encrypted()) {
    return nullptr;
  }
  if (file_view.has_remote_location() && !file_view.main_remote_location().is_web() && input_file == nullptr) {
    int32 flags = 0;
    if (ttl != 0) {
      flags |= telegram_api::inputMediaDocument::TTL_SECONDS_MASK;
    }
    return make_tl_object<telegram_api::inputMediaDocument>(flags, file_view.main_remote_location().as_input_document(),
                                                            ttl);
  }
  if (file_view.has_url()) {
    int32 flags = 0;
    if (ttl != 0) {
      flags |= telegram_api::inputMediaDocumentExternal::TTL_SECONDS_MASK;
    }
    return make_tl_object<telegram_api::inputMediaDocumentExternal>(flags, file_view.url(), ttl);
  }

  if (input_file != nullptr) {
    const Video *video = get_video(file_id);
    CHECK(video != nullptr);

    int32 attribute_flags = 0;
    if (video->supports_streaming) {
      attribute_flags |= telegram_api::documentAttributeVideo::SUPPORTS_STREAMING_MASK;
    }

    vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
    attributes.push_back(make_tl_object<telegram_api::documentAttributeVideo>(
        attribute_flags, false /*ignored*/, false /*ignored*/, video->duration, video->dimensions.width,
        video->dimensions.height));
    if (!video->file_name.empty()) {
      attributes.push_back(make_tl_object<telegram_api::documentAttributeFilename>(video->file_name));
    }
    int32 flags = telegram_api::inputMediaUploadedDocument::NOSOUND_VIDEO_MASK;
    vector<tl_object_ptr<telegram_api::InputDocument>> added_stickers;
    if (video->has_stickers) {
      flags |= telegram_api::inputMediaUploadedDocument::STICKERS_MASK;
      added_stickers = td_->file_manager_->get_input_documents(video->sticker_file_ids);
    }
    string mime_type = video->mime_type;
    if (!begins_with(mime_type, "video/") || ttl > 0) {
      mime_type = "video/mp4";
    }
    if (ttl != 0) {
      flags |= telegram_api::inputMediaUploadedDocument::TTL_SECONDS_MASK;
    }
    if (input_thumbnail != nullptr) {
      flags |= telegram_api::inputMediaUploadedDocument::THUMB_MASK;
    }
    return make_tl_object<telegram_api::inputMediaUploadedDocument>(
        flags, false /*ignored*/, false /*ignored*/, std::move(input_file), std::move(input_thumbnail), mime_type,
        std::move(attributes), std::move(added_stickers), ttl);
  } else {
    CHECK(!file_view.has_remote_location());
  }

  return nullptr;
}

string VideosManager::get_video_search_text(FileId file_id) const {
  auto *video = get_video(file_id);
  CHECK(video != nullptr);
  return video->file_name;
}

}  // namespace td
