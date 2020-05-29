//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/VideosManager.h"

#include "td/telegram/files/FileId.hpp"
#include "td/telegram/Photo.hpp"
#include "td/telegram/Version.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void VideosManager::store_video(FileId file_id, StorerT &storer) const {
  auto it = videos_.find(file_id);
  CHECK(it != videos_.end());
  const Video *video = it->second.get();
  bool has_animated_thumbnail = video->animated_thumbnail.file_id.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(video->has_stickers);
  STORE_FLAG(video->supports_streaming);
  STORE_FLAG(has_animated_thumbnail);
  END_STORE_FLAGS();
  store(video->file_name, storer);
  store(video->mime_type, storer);
  store(video->duration, storer);
  store(video->dimensions, storer);
  store(video->minithumbnail, storer);
  store(video->thumbnail, storer);
  store(file_id, storer);
  if (video->has_stickers) {
    store(video->sticker_file_ids, storer);
  }
  if (has_animated_thumbnail) {
    store(video->animated_thumbnail, storer);
  }
}

template <class ParserT>
FileId VideosManager::parse_video(ParserT &parser) {
  auto video = make_unique<Video>();
  bool has_animated_thumbnail;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(video->has_stickers);
  PARSE_FLAG(video->supports_streaming);
  PARSE_FLAG(has_animated_thumbnail);
  END_PARSE_FLAGS();
  parse(video->file_name, parser);
  parse(video->mime_type, parser);
  parse(video->duration, parser);
  parse(video->dimensions, parser);
  if (parser.version() >= static_cast<int32>(Version::SupportMinithumbnails)) {
    parse(video->minithumbnail, parser);
  }
  parse(video->thumbnail, parser);
  parse(video->file_id, parser);
  if (video->has_stickers) {
    parse(video->sticker_file_ids, parser);
  }
  if (has_animated_thumbnail) {
    parse(video->animated_thumbnail, parser);
  }
  if (parser.get_error() != nullptr || !video->file_id.is_valid()) {
    return FileId();
  }
  return on_get_video(std::move(video), false);
}

}  // namespace td
