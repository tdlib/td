//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#define CHECK(x)                                    \
  if (!(x)) {                                       \
    ::td::detail::do_check(#x, __FILE__, __LINE__); \
  }
#define DCHECK(x) CHECK(x)
namespace td {
namespace detail {
void do_check(const char *message, const char *file, int line);
}
}  // namespace td
