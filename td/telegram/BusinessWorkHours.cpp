//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BusinessWorkHours.h"

#include "td/utils/algorithm.h"
#include "td/utils/format.h"

namespace td {

td_api::object_ptr<td_api::businessWorkHoursInterval>
BusinessWorkHours::WorkHoursInterval::get_business_work_hours_interval_object() const {
  return td_api::make_object<td_api::businessWorkHoursInterval>(start_minute_, end_minute_);
}

telegram_api::object_ptr<telegram_api::businessWeeklyOpen>
BusinessWorkHours::WorkHoursInterval::get_input_business_weekly_open() const {
  return telegram_api::make_object<telegram_api::businessWeeklyOpen>(start_minute_, end_minute_);
}

BusinessWorkHours::BusinessWorkHours(telegram_api::object_ptr<telegram_api::businessWorkHours> &&work_hours) {
  if (work_hours != nullptr) {
    work_hours_ = transform(work_hours->weekly_open_,
                            [](const telegram_api::object_ptr<telegram_api::businessWeeklyOpen> &weekly_open) {
                              return WorkHoursInterval(weekly_open->start_minute_, weekly_open->end_minute_);
                            });
    time_zone_id_ = std::move(work_hours->timezone_id_);
  }
}

BusinessWorkHours::BusinessWorkHours(td_api::object_ptr<td_api::businessWorkHours> &&work_hours) {
  if (work_hours != nullptr) {
    work_hours_ =
        transform(work_hours->work_hours_, [](const td_api::object_ptr<td_api::businessWorkHoursInterval> &interval) {
          return WorkHoursInterval(interval->start_minute_, interval->end_minute_);
        });
    time_zone_id_ = std::move(work_hours->time_zone_id_);
  }
}

bool BusinessWorkHours::is_empty() const {
  return work_hours_.empty();
}

td_api::object_ptr<td_api::businessWorkHours> BusinessWorkHours::get_business_work_hours_object() const {
  if (is_empty()) {
    return nullptr;
  }
  return td_api::make_object<td_api::businessWorkHours>(time_zone_id_,
                                                        transform(work_hours_, [](const WorkHoursInterval &interval) {
                                                          return interval.get_business_work_hours_interval_object();
                                                        }));
}

telegram_api::object_ptr<telegram_api::businessWorkHours> BusinessWorkHours::get_input_business_work_hours() const {
  if (is_empty()) {
    return nullptr;
  }
  return telegram_api::make_object<telegram_api::businessWorkHours>(
      0, false, time_zone_id_, transform(work_hours_, [](const WorkHoursInterval &interval) {
        return interval.get_input_business_weekly_open();
      }));
}

bool operator==(const BusinessWorkHours::WorkHoursInterval &lhs, const BusinessWorkHours::WorkHoursInterval &rhs) {
  return lhs.start_minute_ == rhs.start_minute_ && lhs.end_minute_ == rhs.end_minute_;
}

bool operator!=(const BusinessWorkHours::WorkHoursInterval &lhs, const BusinessWorkHours::WorkHoursInterval &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const BusinessWorkHours::WorkHoursInterval &interval) {
  return string_builder << '[' << interval.start_minute_ << ',' << interval.end_minute_ << ')';
}

bool operator==(const BusinessWorkHours &lhs, const BusinessWorkHours &rhs) {
  return lhs.work_hours_ == rhs.work_hours_ && lhs.time_zone_id_ == rhs.time_zone_id_;
}

bool operator!=(const BusinessWorkHours &lhs, const BusinessWorkHours &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const BusinessWorkHours &work_hours) {
  return string_builder << "BusinessWorkHours[" << work_hours.work_hours_ << " in " << work_hours.time_zone_id_ << ']';
}

}  // namespace td
