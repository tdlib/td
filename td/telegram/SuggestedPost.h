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

  td_api::object_ptr<td_api::SuggestedPostState> get_suggested_post_state_object() const;

 public:
  SuggestedPost() = default;

  SuggestedPost(SuggestedPostPrice price, int32 schedule_date, bool is_accepted, bool is_rejected)
      : price_(std::move(price)), schedule_date_(schedule_date), is_accepted_(is_accepted), is_rejected_(is_rejected) {
  }

  static unique_ptr<SuggestedPost> get_suggested_post(telegram_api::object_ptr<telegram_api::suggestedPost> &&post);

  static Result<unique_ptr<SuggestedPost>> get_suggested_post(
      const Td *td, td_api::object_ptr<td_api::inputSuggestedPostInfo> &&post);

  bool is_pending() const {
    return !is_accepted_ && !is_rejected_;
  }

  int32 get_schedule_date() const {
    return schedule_date_;
  }

  telegram_api::object_ptr<telegram_api::suggestedPost> get_input_suggested_post() const;

  td_api::object_ptr<td_api::suggestedPostInfo> get_suggested_post_info_object(bool can_be_accepted,
                                                                               bool can_be_rejected) const;

  // for draftMessage
  td_api::object_ptr<td_api::inputSuggestedPostInfo> get_input_suggested_post_info_object() const;

  static unique_ptr<SuggestedPost> clone(const unique_ptr<SuggestedPost> &post);

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const SuggestedPost &lhs, const SuggestedPost &rhs);
bool operator!=(const SuggestedPost &lhs, const SuggestedPost &rhs);

}  // namespace td
