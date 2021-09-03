//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
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

class ChannelId {
  int64 id = 0;

 public:
  ChannelId() = default;

  explicit ChannelId(int64 channel_id) : id(channel_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int64>::value>>
  ChannelId(T channel_id) = delete;

  bool is_valid() const {
    return id > 0;  // TODO better is_valid
  }

  int64 get() const {
    return id;
  }

  bool operator==(const ChannelId &other) const {
    return id == other.id;
  }

  bool operator!=(const ChannelId &other) const {
    return id != other.id;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_long(id);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    if (parser.version() >= static_cast<int32>(Version::Support64BitIds)) {
      id = parser.fetch_long();
    } else {
      id = parser.fetch_int();
    }
  }
};

struct ChannelIdHash {
  std::size_t operator()(ChannelId channel_id) const {
    return std::hash<int64>()(channel_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, ChannelId channel_id) {
  return string_builder << "supergroup " << channel_id.get();
}

}  // namespace td
