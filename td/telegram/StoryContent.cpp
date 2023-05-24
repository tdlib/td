//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StoryContent.h"

#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/Photo.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/VideosManager.h"

#include "td/utils/common.h"

namespace td {

class StoryContentPhoto final : public StoryContent {
 public:
  Photo photo_;

  StoryContentPhoto() = default;
  explicit StoryContentPhoto(Photo &&photo) : photo_(std::move(photo)) {
  }

  StoryContentType get_type() const final {
    return StoryContentType::Photo;
  }
};

class StoryContentVideo final : public StoryContent {
 public:
  FileId file_id_;
  FileId alt_file_id_;

  StoryContentVideo() = default;
  StoryContentVideo(FileId file_id, FileId alt_file_id) : file_id_(file_id), alt_file_id_(alt_file_id) {
  }

  StoryContentType get_type() const final {
    return StoryContentType::Video;
  }
};

class StoryContentUnsupported final : public StoryContent {
 public:
  static constexpr int32 CURRENT_VERSION = 1;
  int32 version_ = CURRENT_VERSION;

  StoryContentUnsupported() = default;
  explicit StoryContentUnsupported(int32 version) : version_(version) {
  }

  StoryContentType get_type() const final {
    return StoryContentType::Unsupported;
  }
};

unique_ptr<StoryContent> get_story_content(Td *td, tl_object_ptr<telegram_api::MessageMedia> &&media_ptr,
                                           DialogId owner_dialog_id) {
  CHECK(media_ptr != nullptr);
  int32 constructor_id = media_ptr->get_id();
  switch (constructor_id) {
    case telegram_api::messageMediaPhoto::ID: {
      auto media = move_tl_object_as<telegram_api::messageMediaPhoto>(media_ptr);
      if (media->photo_ == nullptr || (media->flags_ & telegram_api::messageMediaPhoto::TTL_SECONDS_MASK) != 0 ||
          media->spoiler_) {
        LOG(ERROR) << "Receive a story with content " << to_string(media);
        break;
      }

      auto photo = get_photo(td, std::move(media->photo_), owner_dialog_id);
      if (photo.is_empty()) {
        LOG(ERROR) << "Receive a story with empty photo";
        break;
      }
      return make_unique<StoryContentPhoto>(std::move(photo));
    }
    case telegram_api::messageMediaDocument::ID: {
      auto media = move_tl_object_as<telegram_api::messageMediaDocument>(media_ptr);
      if (media->document_ == nullptr || (media->flags_ & telegram_api::messageMediaDocument::TTL_SECONDS_MASK) != 0 ||
          media->spoiler_) {
        LOG(ERROR) << "Receive a story with content " << to_string(media);
        break;
      }

      auto document_ptr = std::move(media->document_);
      int32 document_id = document_ptr->get_id();
      if (document_id == telegram_api::documentEmpty::ID) {
        LOG(ERROR) << "Receive a story with empty document";
        break;
      }
      CHECK(document_id == telegram_api::document::ID);
      auto parsed_document = td->documents_manager_->on_get_document(
          move_tl_object_as<telegram_api::document>(document_ptr), owner_dialog_id, nullptr);
      if (parsed_document.empty() || parsed_document.type != Document::Type::Video) {
        LOG(ERROR) << "Receive a story with " << parsed_document;
        break;
      }
      CHECK(parsed_document.file_id.is_valid());

      FileId alt_file_id;
      if (media->alt_document_ != nullptr) {
        auto alt_document_ptr = std::move(media->alt_document_);
        int32 alt_document_id = alt_document_ptr->get_id();
        if (alt_document_id == telegram_api::documentEmpty::ID) {
          LOG(ERROR) << "Receive alternative " << to_string(alt_document_ptr);
        } else {
          CHECK(alt_document_id == telegram_api::document::ID);
          auto parsed_alt_document = td->documents_manager_->on_get_document(
              move_tl_object_as<telegram_api::document>(alt_document_ptr), owner_dialog_id, nullptr);
          if (parsed_alt_document.empty() || parsed_alt_document.type != Document::Type::Video) {
            LOG(ERROR) << "Receive alternative " << to_string(alt_document_ptr);
          } else {
            alt_file_id = parsed_alt_document.file_id;
          }
        }
      }

      return make_unique<StoryContentVideo>(parsed_document.file_id, alt_file_id);
    }
    case telegram_api::messageMediaUnsupported::ID:
      return make_unique<StoryContentUnsupported>();
    default:
      break;
  }
  return nullptr;
}

Result<unique_ptr<StoryContent>> get_input_story_content(
    Td *td, td_api::object_ptr<td_api::InputStoryContent> &&input_story_content, DialogId owner_dialog_id) {
  LOG(INFO) << "Get input story content from " << to_string(input_story_content);

  switch (input_story_content->get_id()) {
    case td_api::inputStoryContentPhoto::ID: {
      auto input_story = static_cast<const td_api::inputStoryContentPhoto *>(input_story_content.get());
      TRY_RESULT(file_id, td->file_manager_->get_input_file_id(FileType::Photo, input_story->photo_, owner_dialog_id,
                                                               false, false));
      auto sticker_file_ids =
          td->stickers_manager_->get_attached_sticker_file_ids(input_story->added_sticker_file_ids_);
      TRY_RESULT(photo,
                 create_photo(td->file_manager_.get(), file_id, PhotoSize(), 720, 1280, std::move(sticker_file_ids)));
      return make_unique<StoryContentPhoto>(std::move(photo));
    }
    case td_api::inputStoryContentVideo::ID: {
      auto input_story = static_cast<const td_api::inputStoryContentVideo *>(input_story_content.get());
      TRY_RESULT(file_id, td->file_manager_->get_input_file_id(FileType::Video, input_story->video_, owner_dialog_id,
                                                               false, false));
      auto sticker_file_ids =
          td->stickers_manager_->get_attached_sticker_file_ids(input_story->added_sticker_file_ids_);
      bool has_stickers = !sticker_file_ids.empty();
      td->videos_manager_->create_video(file_id, string(), PhotoSize(), AnimationSize(), has_stickers,
                                        std::move(sticker_file_ids), "story.mp4", "video/mp4", input_story->duration_,
                                        get_dimensions(720, 1280, nullptr), true, 0, false);

      return make_unique<StoryContentVideo>(file_id, FileId());
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

void merge_story_contents(Td *td, const StoryContent *old_content, StoryContent *new_content, DialogId dialog_id,
                          bool need_merge_files, bool &is_content_changed, bool &need_update) {
  StoryContentType content_type = new_content->get_type();
  CHECK(old_content->get_type() == content_type);

  switch (content_type) {
    case StoryContentType::Photo: {
      const auto *old_ = static_cast<const StoryContentPhoto *>(old_content);
      auto *new_ = static_cast<StoryContentPhoto *>(new_content);
      merge_photos(td, &old_->photo_, &new_->photo_, dialog_id, need_merge_files, is_content_changed, need_update);
      break;
    }
    case StoryContentType::Video: {
      const auto *old_ = static_cast<const StoryContentVideo *>(old_content);
      const auto *new_ = static_cast<const StoryContentVideo *>(new_content);
      if (old_->file_id_ != new_->file_id_) {
        if (need_merge_files) {
          td->videos_manager_->merge_videos(new_->file_id_, old_->file_id_);
        }
        need_update = true;
      }
      if (old_->alt_file_id_ != new_->alt_file_id_) {
        need_update = true;
      }
      break;
    }
    case StoryContentType::Unsupported: {
      const auto *old_ = static_cast<const StoryContentUnsupported *>(old_content);
      const auto *new_ = static_cast<const StoryContentUnsupported *>(new_content);
      if (old_->version_ != new_->version_) {
        is_content_changed = true;
      }
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

td_api::object_ptr<td_api::StoryContent> get_story_content_object(Td *td, const StoryContent *content) {
  CHECK(content != nullptr);
  switch (content->get_type()) {
    case StoryContentType::Photo: {
      const auto *s = static_cast<const StoryContentPhoto *>(content);
      auto photo = get_photo_object(td->file_manager_.get(), s->photo_);
      if (photo == nullptr) {
        return td_api::make_object<td_api::storyContentUnsupported>();
      }
      return td_api::make_object<td_api::storyContentPhoto>(std::move(photo));
    }
    case StoryContentType::Video: {
      const auto *s = static_cast<const StoryContentVideo *>(content);
      return td_api::make_object<td_api::storyContentVideo>(td->videos_manager_->get_video_object(s->file_id_),
                                                            td->videos_manager_->get_video_object(s->alt_file_id_));
    }
    case StoryContentType::Unsupported:
      return td_api::make_object<td_api::storyContentUnsupported>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

vector<FileId> get_story_content_file_ids(const Td *td, const StoryContent *content) {
  switch (content->get_type()) {
    case StoryContentType::Photo:
      return photo_get_file_ids(static_cast<const StoryContentPhoto *>(content)->photo_);
    case StoryContentType::Video: {
      vector<FileId> result;
      const auto *s = static_cast<const StoryContentVideo *>(content);
      Document(Document::Type::Video, s->file_id_).append_file_ids(td, result);
      Document(Document::Type::Video, s->alt_file_id_).append_file_ids(td, result);
      return result;
    }
    case StoryContentType::Unsupported:
    default:
      return {};
  }
}

int32 get_story_content_duration(const Td *td, const StoryContent *content) {
  CHECK(content != nullptr);
  switch (content->get_type()) {
    case StoryContentType::Video: {
      auto file_id = static_cast<const StoryContentVideo *>(content)->file_id_;
      return td->videos_manager_->get_video_duration(file_id);
    }
    default:
      return -1;
  }
}

}  // namespace td
