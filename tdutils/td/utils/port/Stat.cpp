//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/Stat.h"

#include "td/utils/port/FileFd.h"

#if TD_PORT_POSIX

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/ScopeGuard.h"

#if TD_DARWIN
#include <mach/mach.h>
#include <sys/time.h>
#endif

// We don't want warnings from system headers
#if TD_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include <sys/stat.h>
#if TD_GCC
#pragma GCC diagnostic pop
#endif

#if TD_ANDROID || TD_TIZEN
#include <sys/syscall.h>
#endif

namespace td {
namespace detail {
Stat from_native_stat(const struct ::stat &buf) {
  Stat res;
#if TD_DARWIN
  res.mtime_nsec_ = buf.st_mtimespec.tv_sec * 1000000000ll + buf.st_mtimespec.tv_nsec;  // khm
  res.atime_nsec_ = buf.st_atimespec.tv_sec * 1000000000ll + buf.st_atimespec.tv_nsec;
#else
#if defined(_BSD_SOURCE) || defined(_SVID_SOURCE) || _POSIX_C_SOURCE >= 200809L || _XOPEN_SOURCE >= 700 || TD_EMSCRIPTEN
  res.mtime_nsec_ = buf.st_mtime * 1000000000ll + buf.st_mtim.tv_nsec;
  res.atime_nsec_ = buf.st_atime * 1000000000ll + buf.st_atim.tv_nsec;
#else
  res.mtime_nsec_ = buf.st_mtime * 1000000000ll + buf.st_mtimensec;
  res.atime_nsec_ = buf.st_atime * 1000000000ll + buf.st_atimensec;
#endif
#endif
  res.size_ = buf.st_size;
  res.is_dir_ = (buf.st_mode & S_IFMT) == S_IFDIR;
  res.is_reg_ = (buf.st_mode & S_IFMT) == S_IFREG;
  res.mtime_nsec_ /= 1000;
  res.mtime_nsec_ *= 1000;

  return res;
}

Stat fstat(int native_fd) {
  struct ::stat buf;
  int err = fstat(native_fd, &buf);
  auto fstat_errno = errno;
  LOG_IF(FATAL, err < 0) << Status::PosixError(fstat_errno, PSLICE() << "stat for fd " << native_fd << " failed");
  return detail::from_native_stat(buf);
}

Status update_atime(int native_fd) {
#if TD_LINUX
  timespec times[2];
  // access time
  times[0].tv_nsec = UTIME_NOW;
  // modify time
  times[1].tv_nsec = UTIME_OMIT;
  // if (utimensat(native_fd, nullptr, times, 0) < 0) {
  if (futimens(native_fd, times) < 0) {
    auto status = OS_ERROR(PSLICE() << "futimens " << tag("fd", native_fd));
    LOG(WARNING) << status;
    return status;
  }
  return Status::OK();
#elif TD_DARWIN
  auto info = fstat(native_fd);
  timeval upd[2];
  auto now = Clocks::system();
  // access time
  upd[0].tv_sec = static_cast<decltype(upd[0].tv_sec)>(now);
  upd[0].tv_usec = static_cast<decltype(upd[0].tv_usec)>((now - static_cast<double>(upd[0].tv_sec)) * 1000000);
  // modify time
  upd[1].tv_sec = static_cast<decltype(upd[1].tv_sec)>(info.mtime_nsec_ / 1000000000ll);
  upd[1].tv_usec = static_cast<decltype(upd[1].tv_usec)>(info.mtime_nsec_ % 1000000000ll / 1000);
  if (futimes(native_fd, upd) < 0) {
    auto status = OS_ERROR(PSLICE() << "futimes " << tag("fd", native_fd));
    LOG(WARNING) << status;
    return status;
  }
  return Status::OK();
#else
  return Status::Error("Not supported");
// timespec times[2];
//// access time
// times[0].tv_nsec = UTIME_NOW;
//// modify time
// times[1].tv_nsec = UTIME_OMIT;
////  int err = syscall(__NR_utimensat, native_fd, nullptr, times, 0);
// if (futimens(native_fd, times) < 0) {
//   auto status = OS_ERROR(PSLICE() << "futimens " << tag("fd", native_fd));
//   LOG(WARNING) << status;
//   return status;
// }
// return Status::OK();
#endif
}
}  // namespace detail

Status update_atime(CSlice path) {
  TRY_RESULT(file, FileFd::open(path, FileFd::Flags::Read));
  SCOPE_EXIT {
    file.close();
  };
  return detail::update_atime(file.get_native_fd());
}

Result<Stat> stat(CSlice path) {
  struct ::stat buf;
  if (stat(path.c_str(), &buf) < 0) {
    return OS_ERROR(PSLICE() << "stat for " << tag("file", path) << " failed");
  }
  return detail::from_native_stat(buf);
}

Result<MemStat> mem_stat() {
#if TD_DARWIN
  task_basic_info t_info;
  mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;

  if (KERN_SUCCESS !=
      task_info(mach_task_self(), TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&t_info), &t_info_count)) {
    return Status::Error("task_info failed");
  }
  MemStat res;
  res.resident_size_ = t_info.resident_size;
  res.virtual_size_ = t_info.virtual_size;
  res.resident_size_peak_ = 0;
  res.virtual_size_peak_ = 0;
  return res;
#elif TD_LINUX || TD_ANDROID || TD_TIZEN
  TRY_RESULT(fd, FileFd::open("/proc/self/status", FileFd::Read));
  SCOPE_EXIT {
    fd.close();
  };

  constexpr int TMEM_SIZE = 10000;
  char mem[TMEM_SIZE];
  TRY_RESULT(size, fd.read(MutableSlice(mem, TMEM_SIZE - 1)));
  CHECK(size < TMEM_SIZE - 1);
  mem[size] = 0;

  const char *s = mem;
  MemStat res;
  while (*s) {
    const char *name_begin = s;
    while (*s != 0 && *s != '\n') {
      s++;
    }
    auto name_end = name_begin;
    while (is_alpha(*name_end)) {
      name_end++;
    }
    Slice name(name_begin, name_end);

    uint64 *x = nullptr;
    if (name == "VmPeak") {
      x = &res.virtual_size_peak_;
    }
    if (name == "VmSize") {
      x = &res.virtual_size_;
    }
    if (name == "VmHWM") {
      x = &res.resident_size_peak_;
    }
    if (name == "VmRSS") {
      x = &res.resident_size_;
    }
    if (x != nullptr) {
      Slice value(name_end, s);
      auto r_mem = to_integer_safe<uint64>(value);
      if (r_mem.is_error()) {
        LOG(ERROR) << "Failed to parse memory stats " << tag("name", name) << tag("value", value);
        *x = static_cast<uint64>(-1);
      } else {
        *x = r_mem.ok() * 1024;  // memory is in kB
      }
    }
    if (*s == 0) {
      break;
    }
    s++;
  }

  return res;
#else
  return Status::Error("Not supported");
#endif
}
}  // namespace td
#endif

#if TD_PORT_WINDOWS
namespace td {

Result<Stat> stat(CSlice path) {
  TRY_RESULT(fd, FileFd::open(path, FileFd::Flags::Read));
  return fd.stat();
}

}  // namespace td
#endif
