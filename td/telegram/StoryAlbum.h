//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/StoryAlbumId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

class StoryAlbum {
  StoryAlbumId album_id_;
  string title_;
  Photo icon_photo_;
  FileId icon_video_file_id_;

  friend bool operator==(const StoryAlbum &lhs, const StoryAlbum &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const StoryAlbum &story_album);

 public:
  StoryAlbum() = default;

  StoryAlbum(Td *td, DialogId owner_dialog_id, telegram_api::object_ptr<telegram_api::storyAlbum> &&story_album);

  bool is_valid() const {
    return album_id_.is_valid();
  }

  StoryAlbumId get_story_album_id() const {
    return album_id_;
  }

  vector<FileId> get_file_ids(const Td *td) const;

  td_api::object_ptr<td_api::storyAlbum> get_story_album_object(Td *td) const;
};

bool operator==(const StoryAlbum &lhs, const StoryAlbum &rhs);

inline bool operator!=(const StoryAlbum &lhs, const StoryAlbum &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const StoryAlbum &story_album);

}  // namespace td
