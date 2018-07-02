//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

/*
 * Simple logging.
 *
 * Predefined log levels: FATAL, ERROR, WARNING, INFO, DEBUG
 *
 * LOG(WARNING) << "Hello world!";
 * LOG(INFO) << "Hello " << 1234 << " world!";
 * LOG_IF(INFO, condition) << "Hello world if condition!";
 *
 * Custom log levels may be defined and used using VLOG:
 * int VERBOSITY_NAME(custom) = VERBOSITY_NAME(WARNING);
 * VLOG(custom) << "Hello custom world!"
 *
 * LOG(FATAL) << "Power is off";
 * CHECK(condition) <===> LOG_IF(FATAL, !(condition))
 */

#include "td/utils/common.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/Slice-decl.h"
#include "td/utils/StackAllocator.h"
#include "td/utils/StringBuilder.h"

#include <atomic>
#include <type_traits>

#define PSTR_IMPL() ::td::Logger(::td::NullLog().ref(), 0, true)
#define PSLICE() ::td::detail::Slicify() & PSTR_IMPL()
#define PSTRING() ::td::detail::Stringify() & PSTR_IMPL()
#define PSLICE_SAFE() ::td::detail::SlicifySafe() & PSTR_IMPL()
#define PSTRING_SAFE() ::td::detail::StringifySafe() & PSTR_IMPL()

#define VERBOSITY_NAME(x) verbosity_##x

#define GET_VERBOSITY_LEVEL() (::td::VERBOSITY_NAME(level))
#define SET_VERBOSITY_LEVEL(new_level) (::td::VERBOSITY_NAME(level) = (new_level))

#ifndef STRIP_LOG
#define STRIP_LOG VERBOSITY_NAME(DEBUG)
#endif
#define LOG_IS_STRIPPED(strip_level) \
  (std::integral_constant<int, VERBOSITY_NAME(strip_level)>() > std::integral_constant<int, STRIP_LOG>())

#define LOGGER(level, comment)                                                           \
  ::td::Logger(*::td::log_interface, VERBOSITY_NAME(level), __FILE__, __LINE__, comment, \
               VERBOSITY_NAME(level) == VERBOSITY_NAME(PLAIN))

#define LOG_IMPL(strip_level, level, condition, comment)                                        \
  LOG_IS_STRIPPED(strip_level) || VERBOSITY_NAME(level) > GET_VERBOSITY_LEVEL() || !(condition) \
      ? (void)0                                                                                 \
      : ::td::detail::Voidify() & LOGGER(level, comment)

#define LOG(level) LOG_IMPL(level, level, true, ::td::Slice())
#define LOG_IF(level, condition) LOG_IMPL(level, level, condition, #condition)

#define VLOG(level) LOG_IMPL(DEBUG, level, true, TD_DEFINE_STR(level))
#define VLOG_IF(level, condition) LOG_IMPL(DEBUG, level, condition, TD_DEFINE_STR(level) " " #condition)

#define LOG_ROTATE() ::td::log_interface->rotate()

#define LOG_TAG ::td::Logger::tag_
#define LOG_TAG2 ::td::Logger::tag2_

#if TD_CLANG
bool no_return_func() __attribute__((analyzer_noreturn));
#endif

inline bool no_return_func() {
  return true;
}

// clang-format off
#ifdef CHECK
  #undef CHECK
#endif
#ifdef TD_DEBUG
  #if TD_MSVC
    #define CHECK(condition)            \
      __analysis_assume(!!(condition)); \
      LOG_IMPL(FATAL, FATAL, !(condition), #condition)
  #else
    #define CHECK(condition) LOG_IMPL(FATAL, FATAL, !(condition) && no_return_func(), #condition)
  #endif
#else
  #define CHECK(condition) LOG_IF(NEVER, !(condition))
#endif
// clang-format on

#define UNREACHABLE() \
  LOG(FATAL);         \
  ::td::process_fatal_error("Unreachable in " __FILE__ " at " TD_DEFINE_STR(__LINE__))

constexpr int VERBOSITY_NAME(PLAIN) = -1;
constexpr int VERBOSITY_NAME(FATAL) = 0;
constexpr int VERBOSITY_NAME(ERROR) = 1;
constexpr int VERBOSITY_NAME(WARNING) = 2;
constexpr int VERBOSITY_NAME(INFO) = 3;
constexpr int VERBOSITY_NAME(DEBUG) = 4;
constexpr int VERBOSITY_NAME(NEVER) = 1024;

namespace td {
extern int VERBOSITY_NAME(level);
// TODO Not part of utils. Should be in some separate file
extern int VERBOSITY_NAME(mtproto);
extern int VERBOSITY_NAME(raw_mtproto);
extern int VERBOSITY_NAME(dc);
extern int VERBOSITY_NAME(fd);
extern int VERBOSITY_NAME(net_query);
extern int VERBOSITY_NAME(td_requests);
extern int VERBOSITY_NAME(actor);
extern int VERBOSITY_NAME(buffer);
extern int VERBOSITY_NAME(files);
extern int VERBOSITY_NAME(sqlite);

class LogInterface {
 public:
  LogInterface() = default;
  LogInterface(const LogInterface &) = delete;
  LogInterface &operator=(const LogInterface &) = delete;
  LogInterface(LogInterface &&) = delete;
  LogInterface &operator=(LogInterface &&) = delete;
  virtual ~LogInterface() = default;
  virtual void append(CSlice slice, int log_level_) = 0;
  virtual void rotate() = 0;
};

class NullLog : public LogInterface {
 public:
  void append(CSlice slice, int log_level_) override {
  }
  void rotate() override {
  }
  NullLog &ref() {
    return *this;
  }
};

extern LogInterface *const default_log_interface;
extern LogInterface *log_interface;

using OnFatalErrorCallback = void (*)(CSlice message);
void set_log_fatal_error_callback(OnFatalErrorCallback callback);

[[noreturn]] void process_fatal_error(CSlice message);

#define TC_RED "\x1b[1;31m"
#define TC_BLUE "\x1b[1;34m"
#define TC_CYAN "\x1b[1;36m"
#define TC_GREEN "\x1b[1;32m"
#define TC_YELLOW "\x1b[1;33m"
#define TC_EMPTY "\x1b[0m"

class TsCerr {
 public:
  TsCerr();
  TsCerr(const TsCerr &) = delete;
  TsCerr &operator=(const TsCerr &) = delete;
  TsCerr(TsCerr &&) = delete;
  TsCerr &operator=(TsCerr &&) = delete;
  ~TsCerr();
  TsCerr &operator<<(Slice slice);

 private:
  using Lock = std::atomic_flag;
  static Lock lock_;

  void enterCritical();
  void exitCritical();
};

class Logger {
 public:
  static const int BUFFER_SIZE = 128 * 1024;
  Logger(LogInterface &log, int log_level, bool simple_mode = false)
      : buffer_(StackAllocator::alloc(BUFFER_SIZE))
      , log_(log)
      , log_level_(log_level)
      , sb_(buffer_.as_slice())
      , simple_mode_(simple_mode) {
  }

  Logger(LogInterface &log, int log_level, Slice file_name, int line_num, Slice comment, bool simple_mode);

  template <class T>
  Logger &operator<<(const T &other) {
    sb_ << other;
    return *this;
  }

  MutableCSlice as_cslice() {
    return sb_.as_cslice();
  }
  bool is_error() const {
    return sb_.is_error();
  }
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;
  Logger(Logger &&) = delete;
  Logger &operator=(Logger &&) = delete;
  ~Logger();

  static TD_THREAD_LOCAL const char *tag_;
  static TD_THREAD_LOCAL const char *tag2_;

 private:
  decltype(StackAllocator::alloc(0)) buffer_;
  LogInterface &log_;
  int log_level_;
  StringBuilder sb_;
  bool simple_mode_;
};

namespace detail {
class Voidify {
 public:
  template <class T>
  void operator&(const T &) {
  }
};

class Slicify {
 public:
  CSlice operator&(Logger &logger) {
    return logger.as_cslice();
  }
};

class Stringify {
 public:
  string operator&(Logger &logger) {
    return logger.as_cslice().str();
  }
};
}  // namespace detail

class TsLog : public LogInterface {
 public:
  explicit TsLog(LogInterface *log) : log_(log) {
  }
  void init(LogInterface *log) {
    enter_critical();
    log_ = log;
    exit_critical();
  }
  void append(CSlice slice, int level) override {
    enter_critical();
    log_->append(slice, level);
    exit_critical();
  }
  void rotate() override {
    enter_critical();
    log_->rotate();
    exit_critical();
  }

 private:
  LogInterface *log_ = nullptr;
  std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
  void enter_critical() {
    while (lock_.test_and_set(std::memory_order_acquire)) {
      // spin
    }
  }
  void exit_critical() {
    lock_.clear(std::memory_order_release);
  }
};
}  // namespace td

#include "td/utils/Slice.h"
