//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
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

class StoryId {
  int32 id = 0;

 public:
  StoryId() = default;

  explicit constexpr StoryId(int32 story_id) : id(story_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int32>::value>>
  StoryId(T story_id) = delete;

  static constexpr StoryId max() {
    return StoryId(std::numeric_limits<int32>::max());
  }

  int32 get() const {
    return id;
  }

  bool operator==(const StoryId &other) const {
    return id == other.id;
  }

  bool operator!=(const StoryId &other) const {
    return id != other.id;
  }

  bool is_valid() const {
    return id != 0;
  }

  bool is_server() const {
    return id > 0;
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

struct StoryIdHash {
  uint32 operator()(StoryId story_id) const {
    return Hash<int32>()(story_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, StoryId story_id) {
  return string_builder << "story " << story_id.get();
}

}  // namespace td
