//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageExtendedMedia.h"

#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/VideosManager.h"

#include "td/utils/algorithm.h"

namespace td {

MessageExtendedMedia::MessageExtendedMedia(
    Td *td, telegram_api::object_ptr<telegram_api::MessageExtendedMedia> &&extended_media, FormattedText &&caption,
    DialogId owner_dialog_id) {
  if (extended_media == nullptr) {
    return;
  }
  caption_ = std::move(caption);

  switch (extended_media->get_id()) {
    case telegram_api::messageExtendedMediaPreview::ID: {
      auto media = move_tl_object_as<telegram_api::messageExtendedMediaPreview>(extended_media);
      type_ = Type::Preview;
      duration_ = media->video_duration_;
      dimensions_ = get_dimensions(media->w_, media->h_, "MessageExtendedMedia");
      if (media->thumb_ != nullptr) {
        if (media->thumb_->get_id() == telegram_api::photoStrippedSize::ID) {
          auto thumb = move_tl_object_as<telegram_api::photoStrippedSize>(media->thumb_);
          minithumbnail_ = thumb->bytes_.as_slice().str();
        } else {
          LOG(ERROR) << "Receive " << to_string(media->thumb_);
        }
      }
      break;
    }
    case telegram_api::messageExtendedMedia::ID: {
      auto media = move_tl_object_as<telegram_api::messageExtendedMedia>(extended_media);
      type_ = Type::Unsupported;
      switch (media->media_->get_id()) {
        case telegram_api::messageMediaPhoto::ID: {
          auto photo = move_tl_object_as<telegram_api::messageMediaPhoto>(media->media_);
          if (photo->photo_ == nullptr) {
            break;
          }

          photo_ = get_photo(td->file_manager_.get(), std::move(photo->photo_), owner_dialog_id);
          if (photo_.is_empty()) {
            break;
          }
          type_ = Type::Photo;
          break;
        }
        case telegram_api::messageMediaDocument::ID: {
          auto document = move_tl_object_as<telegram_api::messageMediaDocument>(media->media_);
          if (document->document_ == nullptr) {
            break;
          }

          auto document_ptr = std::move(document->document_);
          int32 document_id = document_ptr->get_id();
          if (document_id == telegram_api::documentEmpty::ID) {
            break;
          }
          CHECK(document_id == telegram_api::document::ID);

          auto parsed_document = td->documents_manager_->on_get_document(
              move_tl_object_as<telegram_api::document>(document_ptr), owner_dialog_id, nullptr);
          if (parsed_document.empty() || parsed_document.type != Document::Type::Video) {
            break;
          }
          CHECK(parsed_document.file_id.is_valid());
          video_file_id_ = parsed_document.file_id;
          type_ = Type::Video;
          break;
        }
        default:
          break;
      }
      break;
    }
    default:
      UNREACHABLE();
  }
}

td_api::object_ptr<td_api::MessageExtendedMedia> MessageExtendedMedia::get_message_extended_media_object(
    Td *td, bool skip_bot_commands, int32 max_media_timestamp) const {
  if (type_ == Type::Empty) {
    return nullptr;
  }

  auto caption = get_formatted_text_object(caption_, skip_bot_commands, max_media_timestamp);
  switch (type_) {
    case Type::Unsupported:
      return td_api::make_object<td_api::messageExtendedMediaUnsupported>(std::move(caption));
    case Type::Preview:
      return td_api::make_object<td_api::messageExtendedMediaPreview>(dimensions_.width, dimensions_.height, duration_,
                                                                      get_minithumbnail_object(minithumbnail_),
                                                                      std::move(caption));
    case Type::Photo: {
      auto photo = get_photo_object(td->file_manager_.get(), photo_);
      CHECK(photo != nullptr);
      return td_api::make_object<td_api::messageExtendedMediaPhoto>(std::move(photo), std::move(caption));
    }
    case Type::Video:
      return td_api::make_object<td_api::messageExtendedMediaVideo>(
          td->videos_manager_->get_video_object(video_file_id_), std::move(caption));
    default:
      UNREACHABLE();
      return nullptr;
  }
}

void MessageExtendedMedia::append_file_ids(const Td *td, vector<FileId> &file_ids) const {
  switch (type_) {
    case Type::Empty:
    case Type::Unsupported:
    case Type::Preview:
      break;
    case Type::Photo:
      append(file_ids, photo_get_file_ids(photo_));
      break;
    case Type::Video:
      Document(Document::Type::Video, video_file_id_).append_file_ids(td, file_ids);
      break;
    default:
      UNREACHABLE();
      break;
  }
}

bool operator==(const MessageExtendedMedia &lhs, const MessageExtendedMedia &rhs) {
  if (lhs.type_ != rhs.type_ || lhs.caption_ != rhs.caption_) {
    return false;
  }
  switch (lhs.type_) {
    case MessageExtendedMedia::Type::Empty:
      return true;
    case MessageExtendedMedia::Type::Unsupported:
      return true;
    case MessageExtendedMedia::Type::Preview:
      return lhs.duration_ == rhs.duration_ && lhs.dimensions_ == rhs.dimensions_ &&
             lhs.minithumbnail_ == rhs.minithumbnail_;
    case MessageExtendedMedia::Type::Photo:
      return lhs.photo_ == rhs.photo_;
    case MessageExtendedMedia::Type::Video:
      return lhs.video_file_id_ == rhs.video_file_id_;
    default:
      UNREACHABLE();
      return true;
  }
}

bool operator!=(const MessageExtendedMedia &lhs, const MessageExtendedMedia &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
