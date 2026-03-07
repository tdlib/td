//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageCover.h"

#include "td/telegram/Td.h"

namespace td {

FileId MessageCover::get_any_file_id() const {
  switch (type_) {
    case Type::Empty:
      return FileId();
    case Type::Photo:
      return get_photo_any_file_id(photo_);
    default:
      UNREACHABLE();
      return FileId();
  }
}

telegram_api::object_ptr<telegram_api::InputMedia> MessageCover::get_cover_input_media(Td *td, bool force,
                                                                                       bool allow_external) const {
  switch (type_) {
    case Type::Photo:
      return photo_get_cover_input_media(td->file_manager_.get(), photo_, force, allow_external);
    case Type::Empty:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

telegram_api::object_ptr<telegram_api::InputMedia> MessageCover::get_input_media(
    Td *td, telegram_api::object_ptr<telegram_api::InputFile> &&input_file) {
  switch (type_) {
    case Type::Photo:
      return photo_get_input_media(td->file_manager_.get(), photo_, std::move(input_file), 0, false);
    case Type::Empty:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

Status MessageCover::merge_with_media(Td *td, DialogId owner_dialog_id,
                                      telegram_api::object_ptr<telegram_api::MessageMedia> &&media_ptr) {
  switch (type_) {
    case Type::Photo: {
      if (media_ptr->get_id() != telegram_api::messageMediaPhoto::ID) {
        return Status::Error(500, "Receive invalid response");
      }
      auto media = telegram_api::move_object_as<telegram_api::messageMediaPhoto>(media_ptr);
      if (media->photo_ == nullptr || media->ttl_seconds_ != 0) {
        return Status::Error(500, "Receive invalid response without photo");
      }
      auto new_photo = get_photo(td, std::move(media->photo_), owner_dialog_id, FileType::Photo);
      if (new_photo.is_empty()) {
        return Status::Error(500, "Receive invalid photo in response");
      }
      bool is_content_changed = false;
      bool need_update = false;
      merge_photos(td, &photo_, &new_photo, owner_dialog_id, true, is_content_changed, need_update);
      break;
    }
    case Type::Empty:
    default:
      UNREACHABLE();
      break;
  }
  return Status::OK();
}

}  // namespace td
