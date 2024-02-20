//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

class BusinessWorkHours {
  struct WorkHoursInterval {
    int32 start_minute_ = 0;
    int32 end_minute_ = 0;

    WorkHoursInterval() = default;
    WorkHoursInterval(int32 start_minute, int32 end_minute) : start_minute_(start_minute), end_minute_(end_minute) {
    }

    td_api::object_ptr<td_api::businessWorkHoursInterval> get_business_work_hours_interval_object() const;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };
  vector<WorkHoursInterval> work_hours_;
  string time_zone_id_;

  friend bool operator==(const WorkHoursInterval &lhs, const WorkHoursInterval &rhs);
  friend bool operator!=(const WorkHoursInterval &lhs, const WorkHoursInterval &rhs);

  friend bool operator==(const BusinessWorkHours &lhs, const BusinessWorkHours &rhs);
  friend bool operator!=(const BusinessWorkHours &lhs, const BusinessWorkHours &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const WorkHoursInterval &interval);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const BusinessWorkHours &work_hours);

 public:
  BusinessWorkHours() = default;

  BusinessWorkHours(telegram_api::object_ptr<telegram_api::businessWorkHours> &&work_hours);

  bool is_empty() const;

  td_api::object_ptr<td_api::businessWorkHours> get_business_work_hours_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const BusinessWorkHours::WorkHoursInterval &lhs, const BusinessWorkHours::WorkHoursInterval &rhs);
bool operator!=(const BusinessWorkHours::WorkHoursInterval &lhs, const BusinessWorkHours::WorkHoursInterval &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const BusinessWorkHours::WorkHoursInterval &interval);

bool operator==(const BusinessWorkHours &lhs, const BusinessWorkHours &rhs);
bool operator!=(const BusinessWorkHours &lhs, const BusinessWorkHours &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const BusinessWorkHours &work_hours);

}  // namespace td
