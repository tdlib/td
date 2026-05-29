//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class FormattedDate {
  enum DateFlags : int32 { Relative = 1, ShortTime = 2, LongTime = 4, ShortDate = 8, LongDate = 16, DayOfWeek = 32 };

  int32 date_ = 0;
  int32 date_flags_ = 0;

  friend bool operator==(const FormattedDate &lhs, const FormattedDate &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const FormattedDate &date);

  static Result<int32> get_date_flags(const string &date_format);

 public:
  FormattedDate() = default;

  FormattedDate(int32 date, bool relative, bool short_time, bool long_time, bool short_date, bool long_date,
                bool day_of_week);

  FormattedDate(int32 date, int32 date_flags);  // tests only

  static Result<FormattedDate> get_formatted_date(int32 date,
                                                  const td_api::object_ptr<td_api::DateTimeFormattingType> &type_ptr);

  static Result<FormattedDate> get_formatted_date(int32 date, const string &date_format);

  bool is_valid() const {
    return date_ > 0;
  }

  int32 get_date() const {
    return date_;
  }

  td_api::object_ptr<td_api::DateTimeFormattingType> get_date_time_formatting_type_object() const;

  telegram_api::object_ptr<telegram_api::messageEntityFormattedDate> get_input_message_entity_formatted_date(
      int32 offset, int32 length) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const FormattedDate &lhs, const FormattedDate &rhs);
bool operator!=(const FormattedDate &lhs, const FormattedDate &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const FormattedDate &date);

}  // namespace td
