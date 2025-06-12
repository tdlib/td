//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/StackAllocator.h"
#include "td/utils/StringBuilder.h"

#include <cerrno>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#define TRY_STATUS(status)                      \
  {                                             \
    auto try_status = (status);                 \
    if (try_status.is_error()) {                \
      return try_status.move_as_error_unsafe(); \
    }                                           \
  }

#define TRY_STATUS_PREFIX(status, prefix)                    \
  {                                                          \
    auto try_status = (status);                              \
    if (try_status.is_error()) {                             \
      return try_status.move_as_error_prefix_unsafe(prefix); \
    }                                                        \
  }

#define TRY_STATUS_PROMISE(promise_name, status)                        \
  {                                                                     \
    auto try_status = (status);                                         \
    if (try_status.is_error()) {                                        \
      return promise_name.set_error(try_status.move_as_error_unsafe()); \
    }                                                                   \
  }

#define TRY_STATUS_PROMISE_PREFIX(promise_name, status, prefix)                      \
  {                                                                                  \
    auto try_status = (status);                                                      \
    if (try_status.is_error()) {                                                     \
      return promise_name.set_error(try_status.move_as_error_prefix_unsafe(prefix)); \
    }                                                                                \
  }

#define TRY_RESULT(name, result) TRY_RESULT_IMPL(TD_CONCAT(TD_CONCAT(r_, name), __LINE__), auto name, result)

#define TRY_RESULT_PROMISE(promise_name, name, result) \
  TRY_RESULT_PROMISE_IMPL(promise_name, TD_CONCAT(TD_CONCAT(r_, name), __LINE__), auto name, result)

#define TRY_RESULT_ASSIGN(name, result) TRY_RESULT_IMPL(TD_CONCAT(r_response, __LINE__), name, result)

#define TRY_RESULT_PROMISE_ASSIGN(promise_name, name, result) \
  TRY_RESULT_PROMISE_IMPL(promise_name, TD_CONCAT(TD_CONCAT(r_, name), __LINE__), name, result)

#define TRY_RESULT_PREFIX(name, result, prefix) \
  TRY_RESULT_PREFIX_IMPL(TD_CONCAT(TD_CONCAT(r_, name), __LINE__), auto name, result, prefix)

#define TRY_RESULT_PREFIX_ASSIGN(name, result, prefix) \
  TRY_RESULT_PREFIX_IMPL(TD_CONCAT(TD_CONCAT(r_, name), __LINE__), name, result, prefix)

#define TRY_RESULT_PROMISE_PREFIX(promise_name, name, result, prefix) \
  TRY_RESULT_PROMISE_PREFIX_IMPL(promise_name, TD_CONCAT(TD_CONCAT(r_, name), __LINE__), auto name, result, prefix)

#define TRY_RESULT_PROMISE_PREFIX_ASSIGN(promise_name, name, result, prefix) \
  TRY_RESULT_PROMISE_PREFIX_IMPL(promise_name, TD_CONCAT(TD_CONCAT(r_, name), __LINE__), name, result, prefix)

#define TRY_RESULT_IMPL(r_name, name, result) \
  auto r_name = (result);                     \
  if (r_name.is_error()) {                    \
    return r_name.move_as_error_unsafe();     \
  }                                           \
  name = r_name.move_as_ok_unsafe();

#define TRY_RESULT_PREFIX_IMPL(r_name, name, result, prefix) \
  auto r_name = (result);                                    \
  if (r_name.is_error()) {                                   \
    return r_name.move_as_error_prefix_unsafe(prefix);       \
  }                                                          \
  name = r_name.move_as_ok_unsafe();

#define TRY_RESULT_PROMISE_IMPL(promise_name, r_name, name, result) \
  auto r_name = (result);                                           \
  if (r_name.is_error()) {                                          \
    return promise_name.set_error(r_name.move_as_error_unsafe());   \
  }                                                                 \
  name = r_name.move_as_ok_unsafe();

#define TRY_RESULT_PROMISE_PREFIX_IMPL(promise_name, r_name, name, result, prefix) \
  auto r_name = (result);                                                          \
  if (r_name.is_error()) {                                                         \
    return promise_name.set_error(r_name.move_as_error_prefix_unsafe(prefix));     \
  }                                                                                \
  name = r_name.move_as_ok_unsafe();

#define LOG_STATUS(status)                             \
  {                                                    \
    auto log_status = (status);                        \
    if (log_status.is_error()) {                       \
      LOG(ERROR) << log_status.move_as_error_unsafe(); \
    }                                                  \
  }

#ifndef TD_STATUS_NO_ENSURE
#define ensure() ensure_impl(__FILE__, __LINE__)
#define ensure_error() ensure_error_impl(__FILE__, __LINE__)
#endif

#if TD_PORT_POSIX
#define OS_ERROR(message)                                    \
  [&] {                                                      \
    auto saved_errno = errno;                                \
    return ::td::Status::PosixError(saved_errno, (message)); \
  }()
#define OS_SOCKET_ERROR(message) OS_ERROR(message)
#elif TD_PORT_WINDOWS
#define OS_ERROR(message)                                      \
  [&] {                                                        \
    auto saved_error = ::GetLastError();                       \
    return ::td::Status::WindowsError(saved_error, (message)); \
  }()
#define OS_SOCKET_ERROR(message)                               \
  [&] {                                                        \
    auto saved_error = ::WSAGetLastError();                    \
    return ::td::Status::WindowsError(saved_error, (message)); \
  }()
#endif

namespace td {

#if TD_PORT_POSIX
CSlice strerror_safe(int code);
#endif

#if TD_PORT_WINDOWS
string winerror_to_string(int code);
#endif

class Status {
  enum class ErrorType : int8 { General, Os };

 public:
  Status() = default;

  bool is_static() const {
    if (is_ok()) {
      return true;
    }
    return get_info().static_flag;
  }

  Status clone() const TD_WARN_UNUSED_RESULT {
    if (is_ok()) {
      return Status();
    }
    auto info = get_info();
    if (info.static_flag) {
      return clone_static(-999);
    }
    return Status(false, info.error_type, info.error_code, message());
  }

  static Status OK() TD_WARN_UNUSED_RESULT {
    return Status();
  }

  static Status Error(int err, Slice message = Slice()) TD_WARN_UNUSED_RESULT {
    return Status(false, ErrorType::General, err, message);
  }

  static Status Error(Slice message) TD_WARN_UNUSED_RESULT {
    return Error(0, message);
  }

#if TD_PORT_WINDOWS
  static Status WindowsError(int saved_error, Slice message) TD_WARN_UNUSED_RESULT {
    return Status(false, ErrorType::Os, saved_error, message);
  }
#endif

#if TD_PORT_POSIX
  static Status PosixError(int32 saved_errno, Slice message) TD_WARN_UNUSED_RESULT {
    return Status(false, ErrorType::Os, saved_errno, message);
  }
#endif

  template <int Code>
  static Status Error() {
    static Status status(true, ErrorType::General, Code, "");
    return status.clone_static(Code);
  }

  StringBuilder &print(StringBuilder &sb) const {
    if (is_ok()) {
      return sb << "OK";
    }
    Info info = get_info();
    switch (info.error_type) {
      case ErrorType::General:
        sb << "[Error";
        break;
      case ErrorType::Os:
#if TD_PORT_POSIX
        sb << "[PosixError : " << strerror_safe(info.error_code);
#elif TD_PORT_WINDOWS
        sb << "[WindowsError : " << winerror_to_string(info.error_code);
#endif
        break;
      default:
        UNREACHABLE();
        break;
    }
    sb << " : " << code() << " : " << message() << "]";
    return sb;
  }

  string to_string() const {
    auto buf = StackAllocator::alloc(4096);
    StringBuilder sb(buf.as_slice());
    print(sb);
    return sb.as_cslice().str();
  }

  // Default interface
  bool is_ok() const TD_WARN_UNUSED_RESULT {
    return !is_error();
  }

  bool is_error() const TD_WARN_UNUSED_RESULT {
    return ptr_ != nullptr;
  }

#ifdef TD_STATUS_NO_ENSURE
  void ensure() const {
    if (!is_ok()) {
      LOG(FATAL) << "Unexpected Status " << to_string();
    }
  }
  void ensure_error() const {
    if (is_ok()) {
      LOG(FATAL) << "Unexpected Status::OK";
    }
  }
#else
  void ensure_impl(CSlice file_name, int line) const {
    if (!is_ok()) {
      LOG(FATAL) << "Unexpected Status " << to_string() << " in file " << file_name << " at line " << line;
    }
  }
  void ensure_error_impl(CSlice file_name, int line) const {
    if (is_ok()) {
      LOG(FATAL) << "Unexpected Status::OK in file " << file_name << " at line " << line;
    }
  }
#endif

  void ignore() const {
    // nop
  }

  int32 code() const {
    if (is_ok()) {
      return 0;
    }
    return get_info().error_code;
  }

  CSlice message() const {
    if (is_ok()) {
      return CSlice("OK");
    }
    return CSlice(ptr_.get() + sizeof(Info));
  }

  string public_message() const {
    if (is_ok()) {
      return "OK";
    }
    Info info = get_info();
    switch (info.error_type) {
      case ErrorType::General:
        return message().str();
      case ErrorType::Os:
#if TD_PORT_POSIX
        return strerror_safe(info.error_code).str();
#elif TD_PORT_WINDOWS
        return winerror_to_string(info.error_code);
#endif
      default:
        UNREACHABLE();
        return "";
    }
  }

  const Status &error() const {
    return *this;
  }

  Status move() TD_WARN_UNUSED_RESULT {
    return std::move(*this);
  }

  Status move_as_error() TD_WARN_UNUSED_RESULT {
    return std::move(*this);
  }

  Status move_as_error_unsafe() TD_WARN_UNUSED_RESULT {
    return std::move(*this);
  }

  Status move_as_ok() = delete;

  Status move_as_ok_unsafe() = delete;

  Status move_as_error_prefix(const Status &status) const TD_WARN_UNUSED_RESULT {
    return status.move_as_error_suffix(message());
  }

  Status move_as_error_prefix(Slice prefix) const TD_WARN_UNUSED_RESULT;

  Status move_as_error_prefix_unsafe(Slice prefix) const TD_WARN_UNUSED_RESULT;

  Status move_as_error_suffix(Slice suffix) const TD_WARN_UNUSED_RESULT;

  Status move_as_error_suffix_unsafe(Slice suffix) const TD_WARN_UNUSED_RESULT;

 private:
  struct Info {
    bool static_flag : 1;
    signed int error_code : 23;
    ErrorType error_type;
  };

  struct Deleter {
    void operator()(char *ptr) {
      if (!get_info(ptr).static_flag) {
        delete[] ptr;
      }
    }
  };
  std::unique_ptr<char[], Deleter> ptr_;

  Status(Info info, Slice message) {
    size_t size = sizeof(Info) + message.size() + 1;
    ptr_ = std::unique_ptr<char[], Deleter>(new char[size]);
    char *ptr = ptr_.get();
    reinterpret_cast<Info *>(ptr)[0] = info;
    ptr += sizeof(Info);
    std::memcpy(ptr, message.begin(), message.size());
    ptr += message.size();
    *ptr = 0;
  }

  Status(bool static_flag, ErrorType error_type, int error_code, Slice message)
      : Status(to_info(static_flag, error_type, error_code), message) {
    if (static_flag) {
      TD_LSAN_IGNORE(ptr_.get());
    }
  }

  Status clone_static(int code) const TD_WARN_UNUSED_RESULT {
    LOG_CHECK(ptr_ != nullptr && get_info().static_flag) << static_cast<const void *>(ptr_.get()) << ' ' << code;
    Status result;
    result.ptr_ = std::unique_ptr<char[], Deleter>(ptr_.get());
    return result;
  }

  static Info to_info(bool static_flag, ErrorType error_type, int error_code) {
    const int MIN_ERROR_CODE = -(1 << 22) + 1;
    const int MAX_ERROR_CODE = (1 << 22) - 1;
    Info tmp;
    tmp.static_flag = static_flag;
    tmp.error_type = error_type;

    if (error_code < MIN_ERROR_CODE) {
      LOG(ERROR) << "Error code value is altered from " << error_code;
      error_code = MIN_ERROR_CODE;
    }
    if (error_code > MAX_ERROR_CODE) {
      LOG(ERROR) << "Error code value is altered from " << error_code;
      error_code = MAX_ERROR_CODE;
    }

#if TD_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
    tmp.error_code = error_code;
#if TD_GCC
#pragma GCC diagnostic pop
#endif
    CHECK(error_code == tmp.error_code);
    return tmp;
  }

  Info get_info() const {
    return get_info(ptr_.get());
  }
  static Info get_info(char *ptr) {
    return reinterpret_cast<Info *>(ptr)[0];
  }
};

template <class T = Unit>
class Result {
 public:
  using ValueT = T;
  Result() : status_(Status::Error<-1>()) {
  }
  template <class S, std::enable_if_t<!std::is_same<std::decay_t<S>, Result>::value, int> = 0>
  Result(S &&x) : status_(), value_(std::forward<S>(x)) {
  }
  struct emplace_t {};
  template <class... ArgsT>
  Result(emplace_t, ArgsT &&...args) : status_(), value_(std::forward<ArgsT>(args)...) {
  }
  Result(Status &&status) : status_(std::move(status)) {
    CHECK(status_.is_error());
  }
  Result(const Result &) = delete;
  Result &operator=(const Result &) = delete;
  Result(Result &&other) noexcept : status_(std::move(other.status_)) {
    if (status_.is_ok()) {
      new (&value_) T(std::move(other.value_));
      other.value_.~T();
    }
    other.status_ = Status::Error<-2>();
  }
  Result &operator=(Result &&other) noexcept {
    CHECK(this != &other);
    if (status_.is_ok()) {
      value_.~T();
    }
    if (other.status_.is_ok()) {
#if TD_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
      new (&value_) T(std::move(other.value_));
#if TD_GCC
#pragma GCC diagnostic pop
#endif
      other.value_.~T();
    }
    status_ = std::move(other.status_);
    other.status_ = Status::Error<-3>();
    return *this;
  }
  template <class... ArgsT>
  void emplace(ArgsT &&...args) {
    if (status_.is_ok()) {
      value_.~T();
    }
    new (&value_) T(std::forward<ArgsT>(args)...);
    status_ = Status::OK();
  }
  ~Result() {
    if (status_.is_ok()) {
      value_.~T();
    }
  }

#ifdef TD_STATUS_NO_ENSURE
  void ensure() const {
    status_.ensure();
  }
  void ensure_error() const {
    status_.ensure_error();
  }
#else
  void ensure_impl(CSlice file_name, int line) const {
    status_.ensure_impl(file_name, line);
  }
  void ensure_error_impl(CSlice file_name, int line) const {
    status_.ensure_error_impl(file_name, line);
  }
#endif
  void ignore() const {
    status_.ignore();
  }
  bool is_ok() const {
    return status_.is_ok();
  }
  bool is_error() const {
    return status_.is_error();
  }
  const Status &error() const {
    CHECK(status_.is_error());
    return status_;
  }
  Status move_as_error() TD_WARN_UNUSED_RESULT {
    CHECK(status_.is_error());
    SCOPE_EXIT {
      status_ = Status::Error<-4>();
    };
    return std::move(status_);
  }
  Status move_as_error_unsafe() TD_WARN_UNUSED_RESULT {
    SCOPE_EXIT {
      status_ = Status::Error<-5>();
    };
    return std::move(status_);
  }
  Status move_as_error_prefix(Slice prefix) TD_WARN_UNUSED_RESULT {
    SCOPE_EXIT {
      status_ = Status::Error<-6>();
    };
    return status_.move_as_error_prefix(prefix);
  }
  Status move_as_error_prefix_unsafe(Slice prefix) TD_WARN_UNUSED_RESULT {
    SCOPE_EXIT {
      status_ = Status::Error<-7>();
    };
    return status_.move_as_error_prefix_unsafe(prefix);
  }
  Status move_as_error_prefix(const Status &prefix) TD_WARN_UNUSED_RESULT {
    SCOPE_EXIT {
      status_ = Status::Error<-8>();
    };
    return status_.move_as_error_prefix(prefix);
  }

  Status move_as_error_suffix(Slice suffix) TD_WARN_UNUSED_RESULT {
    SCOPE_EXIT {
      status_ = Status::Error<-9>();
    };
    return status_.move_as_error_suffix(suffix);
  }
  Status move_as_error_suffix_unsafe(Slice suffix) TD_WARN_UNUSED_RESULT {
    SCOPE_EXIT {
      status_ = Status::Error<-10>();
    };
    return status_.move_as_error_suffix_unsafe(suffix);
  }

  const T &ok() const {
    LOG_CHECK(status_.is_ok()) << status_;
    return value_;
  }
  T &ok_ref() {
    LOG_CHECK(status_.is_ok()) << status_;
    return value_;
  }
  const T &ok_ref() const {
    LOG_CHECK(status_.is_ok()) << status_;
    return value_;
  }
  T move_as_ok() {
    LOG_CHECK(status_.is_ok()) << status_;
    return std::move(value_);
  }
  T move_as_ok_unsafe() {
    return std::move(value_);
  }

  Result<T> clone() const TD_WARN_UNUSED_RESULT {
    if (is_ok()) {
      return Result<T>(ok());  // TODO: return clone(ok());
    }
    return error().clone();
  }
  void clear() {
    *this = Result<T>();
  }

  template <class F>
  Result<decltype(std::declval<F>()(std::declval<T>()))> move_map(F &&f) {
    if (is_error()) {
      return move_as_error_unsafe();
    }
    return f(move_as_ok_unsafe());
  }

  template <class F>
  decltype(std::declval<F>()(std::declval<T>())) move_fmap(F &&f) {
    if (is_error()) {
      return move_as_error_unsafe();
    }
    return f(move_as_ok_unsafe());
  }

 private:
  Status status_;
  union {
    T value_;
  };
};

template <>
inline Result<Unit>::Result(Status &&status) : status_(std::move(status)) {
  // no assert
}

inline StringBuilder &operator<<(StringBuilder &string_builder, const Status &status) {
  return status.print(string_builder);
}

template <class T>
StringBuilder &operator<<(StringBuilder &sb, const Result<T> &result) {
  if (result.is_ok()) {
    return sb << "Ok{" << result.ok() << '}';
  }
  return sb << result.error();
}

template <class T, class S>
bool operator==(const S &a, const Result<T> &b) {
  return b.is_ok() && a == b.ok();
}

template <class T, class S>
bool operator==(const Result<S> &a, const T &b) {
  return a.is_ok() && a.ok() == b;
}

}  // namespace td
