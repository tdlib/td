//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AudiosManager.h"

#include "td/telegram/files/FileId.hpp"
#include "td/telegram/Photo.hpp"

#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class T>
void AudiosManager::store_audio(FileId file_id, T &storer) const {
  auto it = audios_.find(file_id);
  CHECK(it != audios_.end());
  const Audio *audio = it->second.get();
  store(audio->file_name, storer);
  store(audio->mime_type, storer);
  store(audio->duration, storer);
  store(audio->title, storer);
  store(audio->performer, storer);
  store(audio->thumbnail, storer);
  store(file_id, storer);
}

template <class T>
FileId AudiosManager::parse_audio(T &parser) {
  auto audio = make_unique<Audio>();
  parse(audio->file_name, parser);
  parse(audio->mime_type, parser);
  parse(audio->duration, parser);
  parse(audio->title, parser);
  parse(audio->performer, parser);
  parse(audio->thumbnail, parser);
  parse(audio->file_id, parser);
  return on_get_audio(std::move(audio), true);
}

}  // namespace td
