//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/VideosManager.h"

#include "td/telegram/files/FileId.hpp"
#include "td/telegram/Photo.hpp"

#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class T>
void VideosManager::store_video(FileId file_id, T &storer) const {
  auto it = videos_.find(file_id);
  CHECK(it != videos_.end());
  const Video *video = it->second.get();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(video->has_stickers);
  STORE_FLAG(video->supports_streaming);
  END_STORE_FLAGS();
  store(video->file_name, storer);
  store(video->mime_type, storer);
  store(video->duration, storer);
  store(video->dimensions, storer);
  store(video->thumbnail, storer);
  store(file_id, storer);
  if (video->has_stickers) {
    store(video->sticker_file_ids, storer);
  }
}

template <class T>
FileId VideosManager::parse_video(T &parser) {
  auto video = make_unique<Video>();
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(video->has_stickers);
  PARSE_FLAG(video->supports_streaming);
  END_PARSE_FLAGS();
  parse(video->file_name, parser);
  parse(video->mime_type, parser);
  parse(video->duration, parser);
  parse(video->dimensions, parser);
  parse(video->thumbnail, parser);
  parse(video->file_id, parser);
  if (video->has_stickers) {
    parse(video->sticker_file_ids, parser);
  }
  return on_get_video(std::move(video), true);
}

}  // namespace td
