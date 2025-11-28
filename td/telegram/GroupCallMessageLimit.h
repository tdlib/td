//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class GroupCallMessageLimit {
  int64 star_count_ = 0;
  int32 pin_duration_ = 0;
  int32 max_text_length_ = 0;
  int32 max_emoji_count_ = 0;
  int32 color1_ = 0;
  int32 color2_ = 0;
  int32 color_bg_ = 0;

  friend bool operator==(const GroupCallMessageLimit &lhs, const GroupCallMessageLimit &rhs);

  friend bool operator<(const GroupCallMessageLimit &lhs, const GroupCallMessageLimit &rhs);

 public:
  GroupCallMessageLimit() = default;

  explicit GroupCallMessageLimit(telegram_api::object_ptr<telegram_api::JSONValue> &&limit);

  static GroupCallMessageLimit basic();

  bool is_valid() const;

  int64 get_star_count() const {
    return star_count_;
  }

  td_api::object_ptr<td_api::groupCallMessageLevel> get_group_call_message_level_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const GroupCallMessageLimit &lhs, const GroupCallMessageLimit &rhs);

bool operator<(const GroupCallMessageLimit &lhs, const GroupCallMessageLimit &rhs);

class GroupCallMessageLimits {
  vector<GroupCallMessageLimit> limits_;

  friend bool operator==(const GroupCallMessageLimits &lhs, const GroupCallMessageLimits &rhs);

 public:
  GroupCallMessageLimits() = default;

  explicit GroupCallMessageLimits(telegram_api::object_ptr<telegram_api::JSONValue> &&limits);

  static GroupCallMessageLimits basic();

  int32 get_level(int64 star_count) const;

  td_api::object_ptr<td_api::updateGroupCallMessageLevels> get_update_group_call_message_levels_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const GroupCallMessageLimits &lhs, const GroupCallMessageLimits &rhs);

}  // namespace td
