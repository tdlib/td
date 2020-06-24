//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "td/utils/port/rlimit.h"

#include "td/utils/port/config.h"

#include "td/utils/misc.h"

#if TD_PORT_POSIX
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#endif

namespace td {

#if TD_PORT_POSIX

namespace {

int get_rlimit_type(ResourceLimitType rlim_type) {
  switch (rlim_type) {
    case ResourceLimitType::NoFile:
      return RLIMIT_NOFILE;
    case ResourceLimitType::Rss:
      return RLIMIT_RSS;
    default:
      UNREACHABLE();
  }
}

}  // namespace

td::Status set_resource_limit(ResourceLimitType rlim_type, td::uint64 value, td::uint64 cap) {
  if (cap && value > cap) {
    return td::Status::Error("setrlimit(): bad argument");
  }
  int resource = get_rlimit_type(rlim_type);

  struct rlimit r;
  if (getrlimit(resource, &r) < 0) {
    return td::Status::PosixError(errno, "failed getrlimit()");
  }

  if (cap) {
    r.rlim_max = cap;
  } else if (r.rlim_max < value) {
    r.rlim_max = value;
  }
  r.rlim_cur = value;
  if (setrlimit(resource, &r) < 0) {
    return td::Status::PosixError(errno, "failed setrlimit()");
  }
  return td::Status::OK();
}

td::Status set_maximize_resource_limit(ResourceLimitType rlim_type, td::uint64 value) {
  int resource = get_rlimit_type(rlim_type);

  struct rlimit r;
  if (getrlimit(resource, &r) < 0) {
    return td::Status::PosixError(errno, "failed getrlimit()");
  }

  if (r.rlim_max < value) {
    auto t = r;
    t.rlim_cur = value;
    t.rlim_max = value;
    if (setrlimit(resource, &t) >= 0) {
      return td::Status::OK();
    }
  }

  r.rlim_cur = value < r.rlim_max ? value : r.rlim_max;
  if (setrlimit(resource, &r) < 0) {
    return td::Status::PosixError(errno, "failed setrlimit()");
  }
  return td::Status::OK();
}
#else
td::Status set_resource_limit(ResourceLimitType rlim, td::uint64 value) {
  return td::Status::Error("setrlimit not implemented on WINDOWS");
}
td::Status set_maximize_resource_limit(ResourceLimitType rlim, td::uint64 value) {
  return td::Status::OK();
}
#endif

}  // namespace td

