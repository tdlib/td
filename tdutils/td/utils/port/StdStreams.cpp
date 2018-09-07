//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/StdStreams.h"

#include "td/utils/port/detail/NativeFd.h"

namespace td {

namespace {
template <class T>
FileFd create(T handle) {
  return FileFd::from_native_fd(NativeFd(handle, true));
}
}  // namespace
FileFd &Stdin() {
  static FileFd res = create(
#if TD_PORT_POSIX
      0
#elif TD_PORT_WINDOWS
      GetStdHandle(STD_INPUT_HANDLE)
#endif
  );
  return res;
}
FileFd &Stdout() {
  static FileFd res = create(
#if TD_PORT_POSIX
      1
#elif TD_PORT_WINDOWS
      GetStdHandle(STD_OUTPUT_HANDLE)
#endif
  );
  return res;
}
FileFd &Stderr() {
  static FileFd res = create(
#if TD_PORT_POSIX
      2
#elif TD_PORT_WINDOWS
      GetStdHandle(STD_ERROR_HANDLE)
#endif
  );
  return res;
}
}  // namespace td
