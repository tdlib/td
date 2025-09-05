//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StoryAlbum.h"

#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/StoryManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/VideosManager.h"

#include "td/utils/logging.h"

namespace td {

StoryAlbum::StoryAlbum(Td *td, DialogId owner_dialog_id,
                       telegram_api::object_ptr<telegram_api::storyAlbum> &&story_album) {
  CHECK(story_album != nullptr);
  album_id_ = StoryAlbumId(story_album->album_id_);
  if (!album_id_.is_valid()) {
    LOG(ERROR) << "Receive " << album_id_;
    album_id_ = {};
    return;
  }
  title_ = std::move(story_album->title_);
  icon_photo_ = get_photo(td, std::move(story_album->icon_photo_), DialogId(), FileType::PhotoStory);
  if (story_album->icon_video_ != nullptr) {
    auto document_ptr = std::move(story_album->icon_video_);
    switch (document_ptr->get_id()) {
      case telegram_api::documentEmpty::ID:
        LOG(ERROR) << "Receive a story album with empty document";
        break;
      case telegram_api::document::ID: {
        auto parsed_document = td->documents_manager_->on_get_document(
            telegram_api::move_object_as<telegram_api::document>(document_ptr), owner_dialog_id, false, nullptr,
            Document::Type::Video, DocumentsManager::Subtype::Story);
        if (parsed_document.empty() || parsed_document.type != Document::Type::Video) {
          LOG(ERROR) << "Receive a story album with " << parsed_document;
          break;
        }
        icon_video_file_id_ = parsed_document.file_id;
        break;
      }
      default:
        UNREACHABLE();
    }
  }
  td->story_manager_->register_story_album({owner_dialog_id, album_id_}, *this);
}

vector<FileId> StoryAlbum::get_file_ids(const Td *td) const {
  auto file_ids = photo_get_file_ids(icon_photo_);
  Document{Document::Type::Video, icon_video_file_id_}.append_file_ids(td, file_ids);
  return file_ids;
}

td_api::object_ptr<td_api::storyAlbum> StoryAlbum::get_story_album_object(Td *td) const {
  return td_api::make_object<td_api::storyAlbum>(album_id_.get(), title_,
                                                 get_photo_object(td->file_manager_.get(), icon_photo_),
                                                 td->videos_manager_->get_video_object(icon_video_file_id_));
}

bool operator==(const StoryAlbum &lhs, const StoryAlbum &rhs) {
  return lhs.album_id_ == rhs.album_id_ && lhs.title_ == rhs.title_ && lhs.icon_photo_ == rhs.icon_photo_ &&
         lhs.icon_video_file_id_ == rhs.icon_video_file_id_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const StoryAlbum &story_album) {
  return string_builder << story_album.album_id_ << ' ' << story_album.title_;
}

}  // namespace td
