//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/config.h"
#include "td/utils/port/platform.h"

// clang-format off
#if TD_WINDOWS
  #ifndef NTDDI_VERSION
    #define NTDDI_VERSION 0x06020000
  #endif
  #ifndef WINVER
    #define WINVER 0x0602
  #endif
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0602
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef UNICODE
    #define UNICODE
  #endif
  #ifndef _UNICODE
    #define _UNICODE
  #endif
  #ifndef _CRT_SECURE_NO_WARNINGS
    #define _CRT_SECURE_NO_WARNINGS
  #endif

  #include <Winsock2.h>
  #include <ws2tcpip.h>

  #include <Mswsock.h>
  #include <Windows.h>
  #undef ERROR
#endif
// clang-format on

#include "td/utils/int_types.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#define TD_DEBUG

#define TD_DEFINE_STR_IMPL(x) #x
#define TD_DEFINE_STR(x) TD_DEFINE_STR_IMPL(x)
#define TD_CONCAT_IMPL(x, y) x##y
#define TD_CONCAT(x, y) TD_CONCAT_IMPL(x, y)

// clang-format off
#if TD_WINDOWS
  #define TD_DIR_SLASH '\\'
#else
  #define TD_DIR_SLASH '/'
#endif
// clang-format on

namespace td {

inline bool likely(bool x) {
#if TD_CLANG || TD_GCC || TD_INTEL
  return __builtin_expect(x, 1);
#else
  return x;
#endif
}

inline bool unlikely(bool x) {
#if TD_CLANG || TD_GCC || TD_INTEL
  return __builtin_expect(x, 0);
#else
  return x;
#endif
}

// replace std::max and std::min to not have to include <algorithm> everywhere
// as a side bonus, accept parameters by value, so constexpr variables aren't required to be instantiated
template <class T>
T max(T a, T b) {
  return a < b ? b : a;
}

template <class T>
T min(T a, T b) {
  return a < b ? a : b;
}

using string = std::string;

template <class ValueT>
using vector = std::vector<ValueT>;

template <class ValueT>
using unique_ptr = std::unique_ptr<ValueT>;

using std::make_unique;

struct Unit {};

struct Auto {
  template <class ToT>
  operator ToT() const {
    return ToT();
  }
};

template <class ToT, class FromT>
ToT &as(FromT *from) {
  return *reinterpret_cast<ToT *>(from);
}

template <class ToT, class FromT>
const ToT &as(const FromT *from) {
  return *reinterpret_cast<const ToT *>(from);
}

}  // namespace td
