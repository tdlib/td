//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/StoryAlbumId.h"

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/StringBuilder.h"

namespace td {

struct StoryAlbumFullId {
 private:
  DialogId dialog_id;
  StoryAlbumId story_album_id;

 public:
  StoryAlbumFullId() : dialog_id(), story_album_id() {
  }

  StoryAlbumFullId(DialogId dialog_id, StoryAlbumId story_album_id)
      : dialog_id(dialog_id), story_album_id(story_album_id) {
  }

  bool operator==(const StoryAlbumFullId &other) const {
    return dialog_id == other.dialog_id && story_album_id == other.story_album_id;
  }

  bool operator!=(const StoryAlbumFullId &other) const {
    return !(*this == other);
  }

  DialogId get_dialog_id() const {
    return dialog_id;
  }

  StoryAlbumId get_story_album_id() const {
    return story_album_id;
  }

  bool is_valid() const {
    return dialog_id.is_valid() && story_album_id.is_valid();
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    dialog_id.store(storer);
    story_album_id.store(storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    dialog_id.parse(parser);
    story_album_id.parse(parser);
  }
};

struct StoryAlbumFullIdHash {
  uint32 operator()(StoryAlbumFullId story_album_full_id) const {
    return combine_hashes(DialogIdHash()(story_album_full_id.get_dialog_id()),
                          StoryAlbumIdHash()(story_album_full_id.get_story_album_id()));
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, StoryAlbumFullId story_album_full_id) {
  return string_builder << story_album_full_id.get_story_album_id() << " of " << story_album_full_id.get_dialog_id();
}

}  // namespace td
