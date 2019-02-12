//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#define DUMMY_CHECK(condition) ((void)(condition))
#define CHECK(condition)                                               \
  if (!(condition)) {                                                  \
    ::td::detail::process_check_error(#condition, __FILE__, __LINE__); \
  }

#ifdef TD_DEBUG
#define DCHECK CHECK
#else
#define DCHECK DUMMY_CHECK
#endif

#define UNREACHABLE(x) CHECK(false && "unreachable")

namespace td {
namespace detail {
[[noreturn]] void process_check_error(const char *message, const char *file, int line);
}
}  // namespace td
