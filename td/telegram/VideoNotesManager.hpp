//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/VideoNotesManager.h"

#include "td/telegram/files/FileId.hpp"
#include "td/telegram/Photo.hpp"

#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class T>
void VideoNotesManager::store_video_note(FileId file_id, T &storer) const {
  auto it = video_notes_.find(file_id);
  CHECK(it != video_notes_.end());
  const VideoNote *video_note = it->second.get();
  store(video_note->duration, storer);
  store(video_note->dimensions, storer);
  store(video_note->thumbnail, storer);
  store(file_id, storer);
}

template <class T>
FileId VideoNotesManager::parse_video_note(T &parser) {
  auto video_note = make_unique<VideoNote>();
  parse(video_note->duration, parser);
  parse(video_note->dimensions, parser);
  parse(video_note->thumbnail, parser);
  parse(video_note->file_id, parser);
  return on_get_video_note(std::move(video_note), true);
}

}  // namespace td
