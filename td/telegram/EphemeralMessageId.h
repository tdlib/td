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

class EphemeralMessageId {
  int32 id = 0;

 public:
  EphemeralMessageId() = default;

  explicit constexpr EphemeralMessageId(int32 message_id) : id(message_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int32>::value>>
  EphemeralMessageId(T message_id) = delete;

  bool is_valid() const {
    return id > 0;
  }

  int32 get() const {
    return id;
  }

  bool operator==(const EphemeralMessageId &other) const {
    return id == other.id;
  }

  bool operator!=(const EphemeralMessageId &other) const {
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

struct EphemeralMessageIdHash {
  uint32 operator()(EphemeralMessageId ephemeral_message_id) const {
    return Hash<int32>()(ephemeral_message_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, EphemeralMessageId ephemeral_message_id) {
  return string_builder << "ephemeral message " << ephemeral_message_id.get();
}

}  // namespace td
