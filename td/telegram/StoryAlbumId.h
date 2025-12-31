//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

#include <type_traits>

namespace td {

class StoryAlbumId {
  int32 id = 0;

 public:
  StoryAlbumId() = default;

  explicit constexpr StoryAlbumId(int32 stoty_album_id) : id(stoty_album_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int32>::value>>
  StoryAlbumId(T stoty_album_id) = delete;

  int32 get() const {
    return id;
  }

  bool operator==(const StoryAlbumId &other) const {
    return id == other.id;
  }

  bool operator!=(const StoryAlbumId &other) const {
    return id != other.id;
  }

  friend bool operator<(const StoryAlbumId &lhs, const StoryAlbumId &rhs) {
    return lhs.id < rhs.id;
  }

  bool is_valid() const {
    return id > 0;
  }

  static vector<int32> get_story_album_ids_object(const vector<StoryAlbumId> &album_ids) {
    vector<int32> result;
    result.reserve(album_ids.size());
    for (const auto &album_id : album_ids) {
      result.emplace_back(album_id.get());
    }
    return result;
  }

  static vector<int32> get_input_story_album_ids(const vector<StoryAlbumId> &album_ids) {
    return get_story_album_ids_object(album_ids);
  }

  static vector<StoryAlbumId> get_story_album_ids(const vector<int32> &album_ids) {
    vector<StoryAlbumId> result;
    result.reserve(album_ids.size());
    for (const auto &album_id : album_ids) {
      result.push_back(StoryAlbumId(album_id));
    }
    return result;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(id, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(id, parser);
  }
};

struct StoryAlbumIdHash {
  uint32 operator()(StoryAlbumId stoty_album_id) const {
    return Hash<int32>()(stoty_album_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, StoryAlbumId stoty_album_id) {
  return string_builder << "story album " << stoty_album_id.get();
}

}  // namespace td
