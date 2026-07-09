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

#include <type_traits>

namespace td {

class CommunityId {
  int64 id = 0;

 public:
  CommunityId() = default;

  explicit constexpr CommunityId(int64 community_id) : id(community_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int64>::value>>
  CommunityId(T community_id) = delete;

  bool is_valid() const {
    return id > 0;
  }

  int64 get() const {
    return id;
  }

  bool operator==(const CommunityId &other) const {
    return id == other.id;
  }

  bool operator!=(const CommunityId &other) const {
    return id != other.id;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_long(id);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    id = parser.fetch_long();
  }
};

struct CommunityIdHash {
  uint32 operator()(CommunityId community_id) const {
    return Hash<int64>()(community_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, CommunityId community_id) {
  return string_builder << "community " << community_id.get();
}

}  // namespace td
