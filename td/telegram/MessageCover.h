//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class MessageCover {
  enum class Type : int32 { Empty, Photo };
  Type type_ = Type::Empty;
  Photo photo_;

 public:
  MessageCover() = default;

  explicit MessageCover(const Photo &photo) : type_(Type::Photo), photo_(photo) {
  }

  bool is_empty() const {
    return type_ == Type::Empty;
  }

  FileId get_any_file_id() const;

  telegram_api::object_ptr<telegram_api::InputMedia> get_cover_input_media(Td *td, bool force,
                                                                           bool allow_external) const;

  telegram_api::object_ptr<telegram_api::InputMedia> get_input_media(
      Td *td, telegram_api::object_ptr<telegram_api::InputFile> &&input_file);

  Status merge_with_media(Td *td, DialogId owner_dialog_id,
                          telegram_api::object_ptr<telegram_api::MessageMedia> &&media_ptr);
};

}  // namespace td
