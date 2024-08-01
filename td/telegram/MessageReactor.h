//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Dependencies;

class Td;

class MessageReactor {
  DialogId dialog_id_;
  int32 count_ = 0;
  bool is_top_ = false;
  bool is_me_ = false;
  bool is_anonymous_ = false;

  friend bool operator==(const MessageReactor &lhs, const MessageReactor &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const MessageReactor &reactor);

 public:
  MessageReactor() = default;

  explicit MessageReactor(telegram_api::object_ptr<telegram_api::messageReactor> &&reactor);

  bool is_valid() const {
    return (dialog_id_.is_valid() || (!is_me_ && !is_anonymous_)) && count_ > 0 && (is_top_ || is_me_);
  }

  bool is_me() const {
    return is_me_;
  }

  td_api::object_ptr<td_api::paidReactor> get_paid_reactor_object(Td *td) const;

  void add_dependencies(Dependencies &dependencies) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const MessageReactor &lhs, const MessageReactor &rhs);

inline bool operator!=(const MessageReactor &lhs, const MessageReactor &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageReactor &reactor);

}  // namespace td
