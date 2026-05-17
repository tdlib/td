//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/Clocks.h"

#include "td/utils/port/config.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"

#include <chrono>
#include <ctime>

#if TD_PORT_POSIX
#include <time.h>
#endif

namespace td {

double Clocks::monotonic() {
#if TD_PORT_POSIX
  // use system specific functions, because std::chrono::steady_clock is steady only under Windows

#ifdef CLOCK_BOOTTIME
  {
    static bool skip = [] {
      struct timespec spec;
      return clock_gettime(CLOCK_BOOTTIME, &spec) != 0;
    }();
    struct timespec spec;
    if (!skip && clock_gettime(CLOCK_BOOTTIME, &spec) == 0) {
      return static_cast<double>(spec.tv_nsec) * 1e-9 + static_cast<double>(spec.tv_sec);
    }
  }
#endif
#ifdef CLOCK_MONOTONIC_RAW
  {
    static bool skip = [] {
      struct timespec spec;
      return clock_gettime(CLOCK_MONOTONIC_RAW, &spec) != 0;
    }();
    struct timespec spec;
    if (!skip && clock_gettime(CLOCK_MONOTONIC_RAW, &spec) == 0) {
      return static_cast<double>(spec.tv_nsec) * 1e-9 + static_cast<double>(spec.tv_sec);
    }
  }
#endif

#endif

  auto duration = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count()) * 1e-9;
}

double Clocks::system() {
  auto duration = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count()) * 1e-9;
}

string Clocks::get_debug() {
  string result;
#if TD_PORT_POSIX
  auto add_clock = [&](Slice clock_name, clockid_t clock_id) {
    struct timespec spec;
    if (clock_gettime(clock_id, &spec) == 0) {
      result += PSTRING() << '[' << clock_name << " = "
                          << static_cast<double>(spec.tv_nsec) * 1e-9 + static_cast<double>(spec.tv_sec) << ']';
    } else {
      result += PSTRING() << '[' << clock_name << " failed]";
    }
  };

#ifdef CLOCK_REALTIME
  add_clock("REALTIME", CLOCK_REALTIME);
#endif
#ifdef CLOCK_REALTIME_COARSE
  add_clock("REALTIME_COARSE", CLOCK_REALTIME_COARSE);
#endif
#ifdef CLOCK_TAI
  add_clock("TAI", CLOCK_TAI);
#endif
#ifdef CLOCK_MONOTONIC
  add_clock("MONOTONIC", CLOCK_MONOTONIC);
#endif
#ifdef CLOCK_MONOTONIC_COARSE
  add_clock("MONOTONIC_COARSE", CLOCK_MONOTONIC_COARSE);
#endif
#ifdef CLOCK_MONOTONIC_RAW
  add_clock("MONOTONIC_RAW", CLOCK_MONOTONIC_RAW);
#endif
#ifdef CLOCK_BOOTTIME
  add_clock("BOOTTIME", CLOCK_BOOTTIME);
#endif
#ifdef CLOCK_BOOTTIME_ALARM
  add_clock("BOOTTIME_ALARM", CLOCK_BOOTTIME_ALARM);
#endif
#ifdef CLOCK_PROCESS_CPUTIME_ID
  add_clock("PROCESS_CPUTIME_ID", CLOCK_PROCESS_CPUTIME_ID);
#endif
#ifdef CLOCK_THREAD_CPUTIME_ID
  add_clock("THREAD_CPUTIME_ID", CLOCK_THREAD_CPUTIME_ID);
#endif
#endif

  auto duration = std::chrono::steady_clock::now().time_since_epoch();
  result +=
      PSTRING() << "[chrono::steady = "
                << static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count()) * 1e-9
                << ']';
  return result;
}

int Clocks::tz_offset() {
  // not thread-safe on POSIX, so calculate the offset only once
  static int offset = [] {
    auto now = std::time(nullptr);

    auto time_ptr = std::localtime(&now);
    if (time_ptr == nullptr) {
      return 0;
    }
    auto local_time = *time_ptr;

    time_ptr = std::gmtime(&now);
    if (time_ptr == nullptr) {
      return 0;
    }
    auto utc_time = *time_ptr;

    int minute_offset = local_time.tm_min - utc_time.tm_min;
    int hour_offset = local_time.tm_hour - utc_time.tm_hour;
    int day_offset = local_time.tm_mday - utc_time.tm_mday;
    if (day_offset >= 20) {
      day_offset = -1;
    } else if (day_offset <= -20) {
      day_offset = 1;
    }
    int sec_offset = day_offset * 86400 + hour_offset * 3600 + minute_offset * 60;
    if (sec_offset >= 15 * 3600 || sec_offset <= -15 * 3600) {
      return 0;
    }
    return sec_offset / 900 * 900;  // round to 900 just in case
  }();
  return offset;
}

namespace detail {
// may cause static initialization order fiasco for some implementations of std::localtime
// int init_tz_offset_private = Clocks::tz_offset();
}  // namespace detail

}  // namespace td
