//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class TonAmount {
  int64 ton_amount_ = 0;

  friend bool operator==(const TonAmount &lhs, const TonAmount &rhs);

 public:
  TonAmount() = default;

  TonAmount(telegram_api::object_ptr<telegram_api::starsTonAmount> &&amount, bool allow_negative);

  int64 get_ton_amount() const {
    return ton_amount_;
  }

  bool is_positive() const {
    return ton_amount_ > 0;
  }

  static int64 get_ton_count(int64 amount, bool allow_negative);

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const TonAmount &lhs, const TonAmount &rhs);

inline bool operator!=(const TonAmount &lhs, const TonAmount &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const TonAmount &ton_amount);

}  // namespace td
