//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/files/FileId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/SecretInputMedia.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"

#include <unordered_map>

namespace td {

class Td;

class VideoNotesManager {
 public:
  explicit VideoNotesManager(Td *td);

  int32 get_video_note_duration(FileId file_id) const;

  tl_object_ptr<td_api::videoNote> get_video_note_object(FileId file_id);

  void create_video_note(FileId file_id, string minithumbnail, PhotoSize thumbnail, int32 duration,
                         Dimensions dimensions, bool replace);

  tl_object_ptr<telegram_api::InputMedia> get_input_media(FileId file_id,
                                                          tl_object_ptr<telegram_api::InputFile> input_file,
                                                          tl_object_ptr<telegram_api::InputFile> input_thumbnail) const;

  SecretInputMedia get_secret_input_media(FileId video_note_file_id,
                                          tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                          BufferSlice thumbnail, int32 layer) const;

  FileId get_video_note_thumbnail_file_id(FileId file_id) const;

  void delete_video_note_thumbnail(FileId file_id);

  FileId dup_video_note(FileId new_id, FileId old_id);

  bool merge_video_notes(FileId new_id, FileId old_id, bool can_delete_old);

  template <class StorerT>
  void store_video_note(FileId file_id, StorerT &storer) const;

  template <class ParserT>
  FileId parse_video_note(ParserT &parser);

 private:
  class VideoNote {
   public:
    int32 duration = 0;
    Dimensions dimensions;
    string minithumbnail;
    PhotoSize thumbnail;

    FileId file_id;

    bool is_changed = true;
  };

  const VideoNote *get_video_note(FileId file_id) const;

  FileId on_get_video_note(unique_ptr<VideoNote> new_video_note, bool replace);

  Td *td_;
  std::unordered_map<FileId, unique_ptr<VideoNote>, FileIdHash> video_notes_;
};

}  // namespace td
