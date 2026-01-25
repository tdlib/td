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

class StarGiftCollectionId {
  int32 id = 0;

 public:
  StarGiftCollectionId() = default;

  explicit constexpr StarGiftCollectionId(int32 star_gift_collection_id) : id(star_gift_collection_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int32>::value>>
  StarGiftCollectionId(T star_gift_collection_id) = delete;

  static vector<StarGiftCollectionId> get_star_gift_collection_ids(const vector<int32> &input_ids) {
    vector<StarGiftCollectionId> star_gift_collection_ids;
    star_gift_collection_ids.reserve(input_ids.size());
    for (auto &input_id : input_ids) {
      star_gift_collection_ids.emplace_back(input_id);
    }
    return star_gift_collection_ids;
  }

  int32 get() const {
    return id;
  }

  bool operator==(const StarGiftCollectionId &other) const {
    return id == other.id;
  }

  bool operator!=(const StarGiftCollectionId &other) const {
    return id != other.id;
  }

  bool is_valid() const {
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

struct StarGiftCollectionIdHash {
  uint32 operator()(StarGiftCollectionId star_gift_collection_id) const {
    return Hash<int32>()(star_gift_collection_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, StarGiftCollectionId star_gift_collection_id) {
  return string_builder << "gift collection " << star_gift_collection_id.get();
}

}  // namespace td
