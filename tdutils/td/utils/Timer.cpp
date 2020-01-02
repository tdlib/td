//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/Timer.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Time.h"

namespace td {

Timer::Timer() : start_time_(Time::now()) {
}

double Timer::elapsed() const {
  return Time::now() - start_time_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const Timer &timer) {
  return string_builder << "in " << Time::now() - timer.start_time_;
}

PerfWarningTimer::PerfWarningTimer(string name, double max_duration)
    : name_(std::move(name)), start_at_(Time::now()), max_duration_(max_duration) {
}

PerfWarningTimer::PerfWarningTimer(PerfWarningTimer &&other)
    : name_(std::move(other.name_)), start_at_(other.start_at_), max_duration_(other.max_duration_) {
  other.start_at_ = 0;
}

PerfWarningTimer::~PerfWarningTimer() {
  reset();
}

void PerfWarningTimer::reset() {
  if (start_at_ == 0) {
    return;
  }
  double duration = Time::now() - start_at_;
  LOG_IF(WARNING, duration > max_duration_)
      << "SLOW: " << tag("name", name_) << tag("duration", format::as_time(duration));
  start_at_ = 0;
}

}  // namespace td
