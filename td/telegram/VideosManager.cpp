//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/VideosManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/PhotoFormat.h"
#include "td/telegram/secret_api.h"
#include "td/telegram/Td.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"

#include <cmath>

namespace td {

VideosManager::VideosManager(Td *td) : td_(td) {
}

VideosManager::~VideosManager() {
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), videos_);
}

int32 VideosManager::get_video_duration(FileId file_id) const {
  auto video = get_video(file_id);
  CHECK(video != nullptr);
  return video->duration;
}

td_api::object_ptr<td_api::video> VideosManager::get_video_object(FileId file_id) const {
  if (!file_id.is_valid()) {
    return nullptr;
  }

  auto video = get_video(file_id);
  CHECK(video != nullptr);
  auto thumbnail = video->animated_thumbnail.file_id.is_valid()
                       ? get_thumbnail_object(td_->file_manager_.get(), video->animated_thumbnail, PhotoFormat::Mpeg4)
                       : get_thumbnail_object(td_->file_manager_.get(), video->thumbnail, PhotoFormat::Jpeg);
  return td_api::make_object<td_api::video>(video->duration, video->dimensions.width, video->dimensions.height,
                                            video->file_name, video->mime_type, video->has_stickers,
                                            video->supports_streaming, get_minithumbnail_object(video->minithumbnail),
                                            std::move(thumbnail), td_->file_manager_->get_file_object(file_id));
}

td_api::object_ptr<td_api::storyVideo> VideosManager::get_story_video_object(FileId file_id) const {
  if (!file_id.is_valid()) {
    return nullptr;
  }

  auto video = get_video(file_id);
  CHECK(video != nullptr);
  auto thumbnail = video->animated_thumbnail.file_id.is_valid()
                       ? get_thumbnail_object(td_->file_manager_.get(), video->animated_thumbnail, PhotoFormat::Mpeg4)
                       : get_thumbnail_object(td_->file_manager_.get(), video->thumbnail, PhotoFormat::Jpeg);
  return td_api::make_object<td_api::storyVideo>(
      video->precise_duration, video->dimensions.width, video->dimensions.height, video->has_stickers,
      video->is_animation, get_minithumbnail_object(video->minithumbnail), std::move(thumbnail),
      video->preload_prefix_size, video->start_ts, td_->file_manager_->get_file_object(file_id));
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
      v->mime_type = std::move(new_video->mime_type);
    }
    if (v->duration != new_video->duration || v->precise_duration != new_video->precise_duration ||
        v->dimensions != new_video->dimensions || v->supports_streaming != new_video->supports_streaming ||
        v->is_animation != new_video->is_animation || v->preload_prefix_size != new_video->preload_prefix_size ||
        std::fabs(v->start_ts - new_video->start_ts) > 1e-3) {
      LOG(DEBUG) << "Video " << file_id << " info has changed";
      v->duration = new_video->duration;
      v->precise_duration = new_video->precise_duration;
      v->dimensions = new_video->dimensions;
      v->supports_streaming = new_video->supports_streaming;
      v->is_animation = new_video->is_animation;
      v->preload_prefix_size = new_video->preload_prefix_size;
      v->start_ts = new_video->start_ts;
    }
    if (v->file_name != new_video->file_name) {
      LOG(DEBUG) << "Video " << file_id << " file name has changed";
      v->file_name = std::move(new_video->file_name);
    }
    if (v->minithumbnail != new_video->minithumbnail) {
      v->minithumbnail = std::move(new_video->minithumbnail);
    }
    if (v->thumbnail != new_video->thumbnail) {
      if (!v->thumbnail.file_id.is_valid()) {
        LOG(DEBUG) << "Video " << file_id << " thumbnail has changed";
      } else {
        LOG(INFO) << "Video " << file_id << " thumbnail has changed from " << v->thumbnail << " to "
                  << new_video->thumbnail;
      }
      v->thumbnail = std::move(new_video->thumbnail);
    }
    if (v->animated_thumbnail != new_video->animated_thumbnail) {
      if (!v->animated_thumbnail.file_id.is_valid()) {
        LOG(DEBUG) << "Video " << file_id << " animated thumbnail has changed";
      } else {
        LOG(INFO) << "Video " << file_id << " animated thumbnail has changed from " << v->animated_thumbnail << " to "
                  << new_video->animated_thumbnail;
      }
      v->animated_thumbnail = std::move(new_video->animated_thumbnail);
    }
    if (v->has_stickers != new_video->has_stickers && new_video->has_stickers) {
      v->has_stickers = new_video->has_stickers;
    }
    if (v->sticker_file_ids != new_video->sticker_file_ids && !new_video->sticker_file_ids.empty()) {
      v->sticker_file_ids = std::move(new_video->sticker_file_ids);
    }
  }
  return file_id;
}

const VideosManager::Video *VideosManager::get_video(FileId file_id) const {
  return videos_.get_pointer(file_id);
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
  CHECK(new_video == nullptr);
  new_video = make_unique<Video>(*old_video);
  new_video->file_id = new_id;
  new_video->thumbnail.file_id = td_->file_manager_->dup_file_id(new_video->thumbnail.file_id, "dup_video");
  new_video->animated_thumbnail.file_id =
      td_->file_manager_->dup_file_id(new_video->animated_thumbnail.file_id, "dup_video");
  return new_id;
}

void VideosManager::merge_videos(FileId new_id, FileId old_id) {
  CHECK(old_id.is_valid() && new_id.is_valid());
  CHECK(new_id != old_id);

  LOG(INFO) << "Merge videos " << new_id << " and " << old_id;
  const Video *old_ = get_video(old_id);
  CHECK(old_ != nullptr);

  const auto *new_ = get_video(new_id);
  if (new_ == nullptr) {
    dup_video(new_id, old_id);
  } else {
    if (!old_->mime_type.empty() && old_->mime_type != new_->mime_type) {
      LOG(INFO) << "Video has changed: mime_type = (" << old_->mime_type << ", " << new_->mime_type << ")";
    }

    if (old_->thumbnail != new_->thumbnail) {
      //    LOG_STATUS(td_->file_manager_->merge(new_->thumbnail.file_id, old_->thumbnail.file_id));
    }
  }
  LOG_STATUS(td_->file_manager_->merge(new_id, old_id));
}

void VideosManager::create_video(FileId file_id, string minithumbnail, PhotoSize thumbnail,
                                 AnimationSize animated_thumbnail, bool has_stickers, vector<FileId> &&sticker_file_ids,
                                 string file_name, string mime_type, int32 duration, double precise_duration,
                                 Dimensions dimensions, bool supports_streaming, bool is_animation,
                                 int32 preload_prefix_size, double start_ts, bool replace) {
  auto v = make_unique<Video>();
  v->file_id = file_id;
  v->file_name = std::move(file_name);
  v->mime_type = std::move(mime_type);
  v->duration = max(duration, 0);
  v->precise_duration = duration == 0 ? 0.0 : clamp(precise_duration, duration - 1.0, duration + 0.0);
  v->dimensions = dimensions;
  if (!td_->auth_manager_->is_bot()) {
    v->minithumbnail = std::move(minithumbnail);
  }
  v->thumbnail = std::move(thumbnail);
  v->animated_thumbnail = std::move(animated_thumbnail);
  v->supports_streaming = supports_streaming;
  v->is_animation = is_animation;
  v->preload_prefix_size = preload_prefix_size;
  v->start_ts = start_ts;
  v->has_stickers = has_stickers;
  v->sticker_file_ids = std::move(sticker_file_ids);
  on_get_video(std::move(v), replace);
}

SecretInputMedia VideosManager::get_secret_input_media(FileId video_file_id,
                                                       tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                                       const string &caption, BufferSlice thumbnail,
                                                       int32 layer) const {
  const Video *video = get_video(video_file_id);
  CHECK(video != nullptr);
  auto file_view = td_->file_manager_->get_file_view(video_file_id);
  if (!file_view.is_encrypted_secret() || file_view.encryption_key().empty()) {
    return SecretInputMedia{};
  }
  if (file_view.has_remote_location()) {
    input_file = file_view.main_remote_location().as_input_encrypted_file();
  }
  if (!input_file) {
    return {};
  }
  if (video->thumbnail.file_id.is_valid() && thumbnail.empty()) {
    return {};
  }
  vector<tl_object_ptr<secret_api::DocumentAttribute>> attributes;
  attributes.emplace_back(make_tl_object<secret_api::documentAttributeVideo>(
      0, false, video->duration, video->dimensions.width, video->dimensions.height));

  return {std::move(input_file),
          std::move(thumbnail),
          video->thumbnail.dimensions,
          video->mime_type,
          file_view,
          std::move(attributes),
          caption,
          layer};
}

tl_object_ptr<telegram_api::InputMedia> VideosManager::get_input_media(
    FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file,
    tl_object_ptr<telegram_api::InputFile> input_thumbnail, int32 ttl, bool has_spoiler) const {
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
    if (has_spoiler) {
      flags |= telegram_api::inputMediaDocument::SPOILER_MASK;
    }
    return make_tl_object<telegram_api::inputMediaDocument>(
        flags, false /*ignored*/, file_view.main_remote_location().as_input_document(), ttl, string());
  }
  if (file_view.has_url()) {
    int32 flags = 0;
    if (ttl != 0) {
      flags |= telegram_api::inputMediaDocumentExternal::TTL_SECONDS_MASK;
    }
    if (has_spoiler) {
      flags |= telegram_api::inputMediaDocumentExternal::SPOILER_MASK;
    }
    return make_tl_object<telegram_api::inputMediaDocumentExternal>(flags, false /*ignored*/, file_view.url(), ttl);
  }

  if (input_file != nullptr) {
    const Video *video = get_video(file_id);
    CHECK(video != nullptr);

    vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
    {
      int32 attribute_flags = 0;
      if (video->supports_streaming) {
        attribute_flags |= telegram_api::documentAttributeVideo::SUPPORTS_STREAMING_MASK;
      }
      if (video->is_animation) {
        attribute_flags |= telegram_api::documentAttributeVideo::NOSOUND_MASK;
      }
      if (video->start_ts > 0.0) {
        attribute_flags |= telegram_api::documentAttributeVideo::VIDEO_START_TS_MASK;
      }
      attributes.push_back(make_tl_object<telegram_api::documentAttributeVideo>(
          attribute_flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, video->precise_duration,
          video->dimensions.width, video->dimensions.height, 0, video->start_ts));
    }
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
    if (has_spoiler) {
      flags |= telegram_api::inputMediaUploadedDocument::SPOILER_MASK;
    }
    return make_tl_object<telegram_api::inputMediaUploadedDocument>(
        flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, std::move(input_file),
        std::move(input_thumbnail), mime_type, std::move(attributes), std::move(added_stickers), ttl);
  } else {
    CHECK(!file_view.has_remote_location());
  }

  return nullptr;
}

telegram_api::object_ptr<telegram_api::InputMedia> VideosManager::get_story_document_input_media(
    FileId file_id, double main_frame_timestamp) const {
  auto file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.is_encrypted() || !file_view.has_remote_location() || file_view.main_remote_location().is_web()) {
    return nullptr;
  }

  const Video *video = get_video(file_id);
  CHECK(video != nullptr);

  vector<telegram_api::object_ptr<telegram_api::DocumentAttribute>> attributes;
  {
    int32 attribute_flags = 0;
    if (video->supports_streaming) {
      attribute_flags |= telegram_api::documentAttributeVideo::SUPPORTS_STREAMING_MASK;
    }
    if (video->is_animation) {
      attribute_flags |= telegram_api::documentAttributeVideo::NOSOUND_MASK;
    }
    if (main_frame_timestamp > 0.0) {
      attribute_flags |= telegram_api::documentAttributeVideo::VIDEO_START_TS_MASK;
    }
    attributes.push_back(telegram_api::make_object<telegram_api::documentAttributeVideo>(
        attribute_flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, video->precise_duration,
        video->dimensions.width, video->dimensions.height, 0, main_frame_timestamp));
  }
  if (!video->file_name.empty()) {
    attributes.push_back(make_tl_object<telegram_api::documentAttributeFilename>(video->file_name));
  }
  int32 flags = telegram_api::inputMediaUploadedDocument::NOSOUND_VIDEO_MASK;
  vector<telegram_api::object_ptr<telegram_api::InputDocument>> added_stickers;
  if (video->has_stickers) {
    flags |= telegram_api::inputMediaUploadedDocument::STICKERS_MASK;
    added_stickers = td_->file_manager_->get_input_documents(video->sticker_file_ids);
  }
  string mime_type = video->mime_type;
  if (!begins_with(mime_type, "video/")) {
    mime_type = "video/mp4";
  }
  return telegram_api::make_object<telegram_api::inputMediaUploadedDocument>(
      flags, false /*ignored*/, false /*ignored*/, false /*ignored*/,
      telegram_api::make_object<telegram_api::inputFileStoryDocument>(
          file_view.main_remote_location().as_input_document()),
      nullptr, mime_type, std::move(attributes), std::move(added_stickers), 0);
}

string VideosManager::get_video_search_text(FileId file_id) const {
  auto *video = get_video(file_id);
  CHECK(video != nullptr);
  return video->file_name;
}

}  // namespace td
