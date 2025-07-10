//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/SuggestedPostPrice.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class SuggestedPost {
  SuggestedPostPrice price_;
  int32 schedule_date_ = 0;
  bool is_accepted_ = false;
  bool is_rejected_ = false;

  friend bool operator==(const SuggestedPost &lhs, const SuggestedPost &rhs);

 public:
  SuggestedPost() = default;

  static unique_ptr<SuggestedPost> get_suggested_post(telegram_api::object_ptr<telegram_api::suggestedPost> &&post);

  telegram_api::object_ptr<telegram_api::suggestedPost> get_input_suggested_post() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const SuggestedPost &lhs, const SuggestedPost &rhs);
bool operator!=(const SuggestedPost &lhs, const SuggestedPost &rhs);

}  // namespace td
