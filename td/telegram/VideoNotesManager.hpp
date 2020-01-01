//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/VideoNotesManager.h"

#include "td/telegram/files/FileId.hpp"
#include "td/telegram/Photo.hpp"
#include "td/telegram/Version.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void VideoNotesManager::store_video_note(FileId file_id, StorerT &storer) const {
  auto it = video_notes_.find(file_id);
  CHECK(it != video_notes_.end());
  const VideoNote *video_note = it->second.get();
  store(video_note->duration, storer);
  store(video_note->dimensions, storer);
  store(video_note->minithumbnail, storer);
  store(video_note->thumbnail, storer);
  store(file_id, storer);
}

template <class ParserT>
FileId VideoNotesManager::parse_video_note(ParserT &parser) {
  auto video_note = make_unique<VideoNote>();
  parse(video_note->duration, parser);
  parse(video_note->dimensions, parser);
  if (parser.version() >= static_cast<int32>(Version::SupportMinithumbnails)) {
    parse(video_note->minithumbnail, parser);
  }
  parse(video_note->thumbnail, parser);
  parse(video_note->file_id, parser);
  if (parser.get_error() != nullptr || !video_note->file_id.is_valid()) {
    return FileId();
  }
  return on_get_video_note(std::move(video_note), false);
}

}  // namespace td
