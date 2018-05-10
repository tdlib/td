//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/FileFd.h"

#if TD_PORT_WINDOWS
#include "td/utils/misc.h"  // for narrow_cast

#include "td/utils/port/Stat.h"
#include "td/utils/port/wstring_convert.h"
#endif

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/sleep.h"
#include "td/utils/StringBuilder.h"

#include <cstring>

#if TD_PORT_POSIX
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace td {

namespace {

struct PrintFlags {
  int32 flags;
};

StringBuilder &operator<<(StringBuilder &sb, const PrintFlags &print_flags) {
  auto flags = print_flags.flags;
  if (flags &
      ~(FileFd::Write | FileFd::Read | FileFd::Truncate | FileFd::Create | FileFd::Append | FileFd::CreateNew)) {
    return sb << "opened with invalid flags " << flags;
  }

  if (flags & FileFd::Create) {
    sb << "opened/created ";
  } else if (flags & FileFd::CreateNew) {
    sb << "created ";
  } else {
    sb << "opened ";
  }

  if ((flags & FileFd::Write) && (flags & FileFd::Read)) {
    if (flags & FileFd::Append) {
      sb << "for reading and appending";
    } else {
      sb << "for reading and writing";
    }
  } else if (flags & FileFd::Write) {
    if (flags & FileFd::Append) {
      sb << "for appending";
    } else {
      sb << "for writing";
    }
  } else if (flags & FileFd::Read) {
    sb << "for reading";
  } else {
    sb << "for nothing";
  }

  if (flags & FileFd::Truncate) {
    sb << " with truncation";
  }
  return sb;
}

}  // namespace

const Fd &FileFd::get_fd() const {
  return fd_;
}

Fd &FileFd::get_fd() {
  return fd_;
}

Result<FileFd> FileFd::open(CSlice filepath, int32 flags, int32 mode) {
  if (flags & ~(Write | Read | Truncate | Create | Append | CreateNew)) {
    return Status::Error(PSLICE() << "File \"" << filepath << "\" has failed to be " << PrintFlags{flags});
  }

  if ((flags & (Write | Read)) == 0) {
    return Status::Error(PSLICE() << "File \"" << filepath << "\" can't be " << PrintFlags{flags});
  }

#if TD_PORT_POSIX
  int native_flags = 0;

  if ((flags & Write) && (flags & Read)) {
    native_flags |= O_RDWR;
  } else if (flags & Write) {
    native_flags |= O_WRONLY;
  } else {
    CHECK(flags & Read);
    native_flags |= O_RDONLY;
  }

  if (flags & Truncate) {
    native_flags |= O_TRUNC;
  }

  if (flags & Create) {
    native_flags |= O_CREAT;
  } else if (flags & CreateNew) {
    native_flags |= O_CREAT;
    native_flags |= O_EXCL;
  }

  if (flags & Append) {
    native_flags |= O_APPEND;
  }

  int native_fd = skip_eintr([&] { return ::open(filepath.c_str(), native_flags, static_cast<mode_t>(mode)); });
  if (native_fd < 0) {
    return OS_ERROR(PSLICE() << "File \"" << filepath << "\" can't be " << PrintFlags{flags});
  }

  FileFd result;
  result.fd_ = Fd(native_fd, Fd::Mode::Owner);
#elif TD_PORT_WINDOWS
  // TODO: support modes
  auto r_filepath = to_wstring(filepath);
  if (r_filepath.is_error()) {
    return Status::Error(PSLICE() << "Failed to convert file path \" << filepath << \" to UTF-16");
  }
  auto w_filepath = r_filepath.move_as_ok();
  DWORD desired_access = 0;
  if ((flags & Write) && (flags & Read)) {
    desired_access |= GENERIC_READ | GENERIC_WRITE;
  } else if (flags & Write) {
    desired_access |= GENERIC_WRITE;
  } else {
    CHECK(flags & Read);
    desired_access |= GENERIC_READ;
  }

  // TODO: share mode
  DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE;

  DWORD creation_disposition = 0;
  if (flags & Create) {
    if (flags & Truncate) {
      creation_disposition = CREATE_ALWAYS;
    } else {
      creation_disposition = OPEN_ALWAYS;
    }
  } else if (flags & CreateNew) {
    creation_disposition = CREATE_NEW;
  } else {
    if (flags & Truncate) {
      creation_disposition = TRUNCATE_EXISTING;
    } else {
      creation_disposition = OPEN_EXISTING;
    }
  }

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM)
  auto handle = CreateFile(w_filepath.c_str(), desired_access, share_mode, nullptr, creation_disposition, 0, nullptr);
#else
  auto handle = CreateFile2(w_filepath.c_str(), desired_access, share_mode, creation_disposition, nullptr);
#endif
  if (handle == INVALID_HANDLE_VALUE) {
    return OS_ERROR(PSLICE() << "File \"" << filepath << "\" can't be " << PrintFlags{flags});
  }
  if (flags & Append) {
    LARGE_INTEGER offset;
    offset.QuadPart = 0;
    auto set_pointer_res = SetFilePointerEx(handle, offset, nullptr, FILE_END);
    if (!set_pointer_res) {
      auto res = OS_ERROR(PSLICE() << "Failed to seek to the end of file \"" << filepath << "\"");
      CloseHandle(handle);
      return res;
    }
  }
  FileFd result;
  result.fd_ = Fd::create_file_fd(handle);
#endif
  result.fd_.update_flags(Fd::Flag::Write);
  return std::move(result);
}

Result<size_t> FileFd::write(Slice slice) {
#if TD_PORT_POSIX
  CHECK(!fd_.empty());
  int native_fd = get_native_fd();
  auto write_res = skip_eintr([&] { return ::write(native_fd, slice.begin(), slice.size()); });
  if (write_res >= 0) {
    return narrow_cast<size_t>(write_res);
  }

  auto write_errno = errno;
  auto error = Status::PosixError(write_errno, PSLICE() << "Write to [fd = " << native_fd << "] has failed");
  if (write_errno != EAGAIN
#if EAGAIN != EWOULDBLOCK
      && write_errno != EWOULDBLOCK
#endif
      && write_errno != EIO) {
    LOG(ERROR) << error;
  }
  return std::move(error);
#elif TD_PORT_WINDOWS
  return fd_.write(slice);
#endif
}

Result<size_t> FileFd::read(MutableSlice slice) {
#if TD_PORT_POSIX
  CHECK(!fd_.empty());
  int native_fd = get_native_fd();
  auto read_res = skip_eintr([&] { return ::read(native_fd, slice.begin(), slice.size()); });
  auto read_errno = errno;

  if (read_res >= 0) {
    if (narrow_cast<size_t>(read_res) < slice.size()) {
      fd_.clear_flags(Read);
    }
    return static_cast<size_t>(read_res);
  }

  auto error = Status::PosixError(read_errno, PSLICE() << "Read from [fd = " << native_fd << "] has failed");
  if (read_errno != EAGAIN
#if EAGAIN != EWOULDBLOCK
      && read_errno != EWOULDBLOCK
#endif
      && read_errno != EIO) {
    LOG(ERROR) << error;
  }
  return std::move(error);
#elif TD_PORT_WINDOWS
  return fd_.read(slice);
#endif
}

Result<size_t> FileFd::pwrite(Slice slice, int64 offset) {
  if (offset < 0) {
    return Status::Error("Offset must be non-negative");
  }
#if TD_PORT_POSIX
  TRY_RESULT(offset_off_t, narrow_cast_safe<off_t>(offset));
  CHECK(!fd_.empty());
  int native_fd = get_native_fd();
  auto pwrite_res = skip_eintr([&] { return ::pwrite(native_fd, slice.begin(), slice.size(), offset_off_t); });
  if (pwrite_res >= 0) {
    return narrow_cast<size_t>(pwrite_res);
  }

  auto pwrite_errno = errno;
  auto error = Status::PosixError(
      pwrite_errno, PSLICE() << "Pwrite to [fd = " << native_fd << "] at [offset = " << offset << "] has failed");
  if (pwrite_errno != EAGAIN
#if EAGAIN != EWOULDBLOCK
      && pwrite_errno != EWOULDBLOCK
#endif
      && pwrite_errno != EIO) {
    LOG(ERROR) << error;
  }
  return std::move(error);
#elif TD_PORT_WINDOWS
  DWORD bytes_written = 0;
  OVERLAPPED overlapped;
  std::memset(&overlapped, 0, sizeof(overlapped));
  overlapped.Offset = static_cast<DWORD>(offset);
  overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
  auto res =
      WriteFile(fd_.get_io_handle(), slice.data(), narrow_cast<DWORD>(slice.size()), &bytes_written, &overlapped);
  if (!res) {
    return OS_ERROR("Failed to pwrite");
  }
  return bytes_written;
#endif
}

Result<size_t> FileFd::pread(MutableSlice slice, int64 offset) {
  if (offset < 0) {
    return Status::Error("Offset must be non-negative");
  }
#if TD_PORT_POSIX
  TRY_RESULT(offset_off_t, narrow_cast_safe<off_t>(offset));
  CHECK(!fd_.empty());
  int native_fd = get_native_fd();
  auto pread_res = skip_eintr([&] { return ::pread(native_fd, slice.begin(), slice.size(), offset_off_t); });
  if (pread_res >= 0) {
    return narrow_cast<size_t>(pread_res);
  }

  auto pread_errno = errno;
  auto error = Status::PosixError(
      pread_errno, PSLICE() << "Pread from [fd = " << native_fd << "] at [offset = " << offset << "] has failed");
  if (pread_errno != EAGAIN
#if EAGAIN != EWOULDBLOCK
      && pread_errno != EWOULDBLOCK
#endif
      && pread_errno != EIO) {
    LOG(ERROR) << error;
  }
  return std::move(error);
#elif TD_PORT_WINDOWS
  DWORD bytes_read = 0;
  OVERLAPPED overlapped;
  std::memset(&overlapped, 0, sizeof(overlapped));
  overlapped.Offset = static_cast<DWORD>(offset);
  overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
  auto res = ReadFile(fd_.get_io_handle(), slice.data(), narrow_cast<DWORD>(slice.size()), &bytes_read, &overlapped);
  if (!res) {
    return OS_ERROR("Failed to pread");
  }
  return bytes_read;
#endif
}

Status FileFd::lock(FileFd::LockFlags flags, int32 max_tries) {
  if (max_tries <= 0) {
    return Status::Error(0, "Can't lock file: wrong max_tries");
  }

  while (true) {
#if TD_PORT_POSIX
    struct flock lock;
    std::memset(&lock, 0, sizeof(lock));

    lock.l_type = static_cast<short>([&] {
      switch (flags) {
        case LockFlags::Read:
          return F_RDLCK;
        case LockFlags::Write:
          return F_WRLCK;
        case LockFlags::Unlock:
          return F_UNLCK;
        default:
          UNREACHABLE();
          return F_UNLCK;
      }
    }());

    lock.l_whence = SEEK_SET;
    if (fcntl(get_native_fd(), F_SETLK, &lock) == -1) {
      if (errno == EAGAIN) {
#elif TD_PORT_WINDOWS
    OVERLAPPED overlapped;
    std::memset(&overlapped, 0, sizeof(overlapped));

    BOOL result;
    if (flags == LockFlags::Unlock) {
      result = UnlockFileEx(fd_.get_io_handle(), 0, MAXDWORD, MAXDWORD, &overlapped);
    } else {
      DWORD dw_flags = LOCKFILE_FAIL_IMMEDIATELY;
      if (flags == LockFlags::Write) {
        dw_flags |= LOCKFILE_EXCLUSIVE_LOCK;
      }

      result = LockFileEx(fd_.get_io_handle(), dw_flags, 0, MAXDWORD, MAXDWORD, &overlapped);
    }

    if (!result) {
      if (GetLastError() == ERROR_LOCK_VIOLATION) {
#endif
        if (--max_tries > 0) {
          usleep_for(100000);
          continue;
        }

        return OS_ERROR("Can't lock file because it is already in use; check for another program instance running");
      }

      return OS_ERROR("Can't lock file");
    }
    return Status::OK();
  }
}

void FileFd::close() {
  fd_.close();
}

bool FileFd::empty() const {
  return fd_.empty();
}

#if TD_PORT_POSIX
int FileFd::get_native_fd() const {
  return fd_.get_native_fd();
}
#endif

int32 FileFd::get_flags() const {
  return fd_.get_flags();
}

void FileFd::update_flags(Fd::Flags mask) {
  fd_.update_flags(mask);
}

int64 FileFd::get_size() {
  return stat().size_;
}

#if TD_PORT_WINDOWS
static uint64 filetime_to_unix_time_nsec(LONGLONG filetime) {
  const auto FILETIME_UNIX_TIME_DIFF = 116444736000000000ll;
  return static_cast<uint64>((filetime - FILETIME_UNIX_TIME_DIFF) * 100);
}
#endif

Stat FileFd::stat() {
  CHECK(!empty());
#if TD_PORT_POSIX
  return detail::fstat(get_native_fd());
#elif TD_PORT_WINDOWS
  Stat res;

  FILE_BASIC_INFO basic_info;
  auto status = GetFileInformationByHandleEx(fd_.get_io_handle(), FileBasicInfo, &basic_info, sizeof(basic_info));
  if (!status) {
    auto error = OS_ERROR("Stat failed");
    LOG(FATAL) << error;
  }
  res.atime_nsec_ = filetime_to_unix_time_nsec(basic_info.LastAccessTime.QuadPart);
  res.mtime_nsec_ = filetime_to_unix_time_nsec(basic_info.LastWriteTime.QuadPart);
  res.is_dir_ = (basic_info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  res.is_reg_ = true;

  FILE_STANDARD_INFO standard_info;
  status = GetFileInformationByHandleEx(fd_.get_io_handle(), FileStandardInfo, &standard_info, sizeof(standard_info));
  if (!status) {
    auto error = OS_ERROR("Stat failed");
    LOG(FATAL) << error;
  }
  res.size_ = standard_info.EndOfFile.QuadPart;

  return res;
#endif
}

Status FileFd::sync() {
  CHECK(!empty());
#if TD_PORT_POSIX
  if (fsync(fd_.get_native_fd()) != 0) {
#elif TD_PORT_WINDOWS
  if (FlushFileBuffers(fd_.get_io_handle()) == 0) {
#endif
    return OS_ERROR("Sync failed");
  }
  return Status::OK();
}

Status FileFd::seek(int64 position) {
  CHECK(!empty());
#if TD_PORT_POSIX
  TRY_RESULT(position_off_t, narrow_cast_safe<off_t>(position));
  if (skip_eintr([&] { return ::lseek(fd_.get_native_fd(), position_off_t, SEEK_SET); }) < 0) {
#elif TD_PORT_WINDOWS
  LARGE_INTEGER offset;
  offset.QuadPart = position;
  if (SetFilePointerEx(fd_.get_io_handle(), offset, nullptr, FILE_BEGIN) == 0) {
#endif
    return OS_ERROR("Seek failed");
  }
  return Status::OK();
}

Status FileFd::truncate_to_current_position(int64 current_position) {
  CHECK(!empty());
#if TD_PORT_POSIX
  TRY_RESULT(current_position_off_t, narrow_cast_safe<off_t>(current_position));
  if (skip_eintr([&] { return ::ftruncate(fd_.get_native_fd(), current_position_off_t); }) < 0) {
#elif TD_PORT_WINDOWS
  if (SetEndOfFile(fd_.get_io_handle()) == 0) {
#endif
    return OS_ERROR("Truncate failed");
  }
  return Status::OK();
}

}  // namespace td
