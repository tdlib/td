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

class DialogFilterId {
  int32 id = 0;

 public:
  DialogFilterId() = default;

  explicit DialogFilterId(int32 dialog_filter_id) : id(dialog_filter_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int32>::value>>
  DialogFilterId(T dialog_filter_id) = delete;

  bool is_valid() const {
    return id > 0;
  }

  int32 get() const {
    return id;
  }

  bool operator==(const DialogFilterId &other) const {
    return id == other.id;
  }

  bool operator!=(const DialogFilterId &other) const {
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

struct DialogFilterIdHash {
  std::size_t operator()(DialogFilterId dialog_filter_id) const {
    return std::hash<int32>()(dialog_filter_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, DialogFilterId dialog_filter_id) {
  return string_builder << "chat filter " << dialog_filter_id.get();
}

}  // namespace td
