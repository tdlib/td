//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

#include <type_traits>

namespace td {

class MessageTtlSetting {
  int32 ttl_period_ = 0;

  friend StringBuilder &operator<<(StringBuilder &string_builder, const MessageTtlSetting &message_ttl_setting);

  friend bool operator==(const MessageTtlSetting &lhs, const MessageTtlSetting &rhs);

 public:
  MessageTtlSetting() = default;

  template <class T, typename = std::enable_if_t<std::is_convertible<T, int32>::value>>
  MessageTtlSetting(T ttl_period) = delete;

  explicit MessageTtlSetting(int32 ttl_period) : ttl_period_(ttl_period) {
  }

  bool is_empty() const;

  int32 get_message_ttl_setting_object() const;

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(ttl_period_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(ttl_period_, parser);
  }
};

bool operator==(const MessageTtlSetting &lhs, const MessageTtlSetting &rhs);

inline bool operator!=(const MessageTtlSetting &lhs, const MessageTtlSetting &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageTtlSetting &message_ttl_setting);

}  // namespace td
