//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

#include <functional>
#include <type_traits>

namespace td {

class UserId {
  int32 id = 0;

 public:
  UserId() = default;

  explicit UserId(int32 user_id) : id(user_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int32>::value>>
  UserId(T user_id) = delete;

  bool is_valid() const {
    return id > 0;
  }

  int32 get() const {
    return id;
  }

  bool operator==(const UserId &other) const {
    return id == other.id;
  }

  bool operator!=(const UserId &other) const {
    return id != other.id;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_int(id);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    id = parser.fetch_int();
  }
};

struct UserIdHash {
  std::size_t operator()(UserId user_id) const {
    return std::hash<int32>()(user_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, UserId user_id) {
  return string_builder << "user " << user_id.get();
}

}  // namespace td
