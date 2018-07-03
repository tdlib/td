//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AnimationsManager.h"

#include "td/telegram/files/FileId.hpp"
#include "td/telegram/Photo.hpp"
#include "td/telegram/Version.h"

#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class T>
void AnimationsManager::store_animation(FileId file_id, T &storer) const {
  auto it = animations_.find(file_id);
  CHECK(it != animations_.end());
  const Animation *animation = it->second.get();
  store(animation->duration, storer);
  store(animation->dimensions, storer);
  store(animation->file_name, storer);
  store(animation->mime_type, storer);
  store(animation->thumbnail, storer);
  store(file_id, storer);
}

template <class T>
FileId AnimationsManager::parse_animation(T &parser) {
  auto animation = make_unique<Animation>();
  if (parser.version() >= static_cast<int32>(Version::AddDurationToAnimation)) {
    parse(animation->duration, parser);
  }
  parse(animation->dimensions, parser);
  parse(animation->file_name, parser);
  parse(animation->mime_type, parser);
  parse(animation->thumbnail, parser);
  parse(animation->file_id, parser);
  return on_get_animation(std::move(animation), false);
}

}  // namespace td
