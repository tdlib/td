//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

#include <functional>
#include <type_traits>

namespace td {

class BackgroundId {
  int64 id = 0;

 public:
  BackgroundId() = default;

  explicit BackgroundId(int64 background_id) : id(background_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int64>::value>>
  BackgroundId(T background_id) = delete;

  int64 get() const {
    return id;
  }

  bool operator==(const BackgroundId &other) const {
    return id == other.id;
  }

  bool operator!=(const BackgroundId &other) const {
    return id != other.id;
  }

  bool is_valid() const {
    return id != 0;
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

struct BackgroundIdHash {
  std::size_t operator()(BackgroundId background_id) const {
    return std::hash<int64>()(background_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, BackgroundId background_id) {
  return string_builder << "background " << background_id.get();
}

}  // namespace td
