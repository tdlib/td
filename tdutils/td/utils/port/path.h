//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/config.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <utility>

#if TD_PORT_POSIX
#include <limits.h>
#include <dirent.h>
#include <sys/types.h>
#endif

#if TD_DARWIN
#include <sys/syslimits.h>
#endif

#if TD_PORT_WINDOWS
#include "td/utils/port/wstring_convert.h"

#include <string>
#endif

namespace td {

Status mkdir(CSlice dir, int32 mode = 0700) TD_WARN_UNUSED_RESULT;
Status mkpath(CSlice path, int32 mode = 0700) TD_WARN_UNUSED_RESULT;
Status rename(CSlice from, CSlice to) TD_WARN_UNUSED_RESULT;
Result<string> realpath(CSlice slice, bool ignore_access_denied = false) TD_WARN_UNUSED_RESULT;
Status chdir(CSlice dir) TD_WARN_UNUSED_RESULT;
Status rmdir(CSlice dir) TD_WARN_UNUSED_RESULT;
Status unlink(CSlice path) TD_WARN_UNUSED_RESULT;
Status set_temporary_dir(CSlice dir) TD_WARN_UNUSED_RESULT;
CSlice get_temporary_dir();
Result<std::pair<FileFd, string>> mkstemp(CSlice dir) TD_WARN_UNUSED_RESULT;
Result<string> mkdtemp(CSlice dir, Slice prefix) TD_WARN_UNUSED_RESULT;

template <class Func>
Status walk_path(CSlice path, Func &func) TD_WARN_UNUSED_RESULT;

#if TD_PORT_POSIX

// TODO move details somewhere else
namespace detail {
template <class Func>
Status walk_path_dir(string &path, FileFd fd, Func &&func) TD_WARN_UNUSED_RESULT;
template <class Func>
Status walk_path_dir(string &path, Func &&func) TD_WARN_UNUSED_RESULT;
template <class Func>
Status walk_path_file(string &path, Func &&func) TD_WARN_UNUSED_RESULT;
template <class Func>
Status walk_path(string &path, Func &&func) TD_WARN_UNUSED_RESULT;

template <class Func>
Status walk_path_subdir(string &path, DIR *dir, Func &&func) {
  while (true) {
    errno = 0;
    auto *entry = readdir(dir);
    auto readdir_errno = errno;
    if (readdir_errno) {
      return Status::PosixError(readdir_errno, "readdir");
    }
    if (entry == nullptr) {
      return Status::OK();
    }
    Slice name = Slice(&*entry->d_name);
    if (name == "." || name == "..") {
      continue;
    }
    auto size = path.size();
    if (path.back() != TD_DIR_SLASH) {
      path += TD_DIR_SLASH;
    }
    path.append(name.begin(), name.size());
    SCOPE_EXIT {
      path.resize(size);
    };
    Status status;
#ifdef DT_DIR
    if (entry->d_type == DT_UNKNOWN) {
      status = walk_path(path, std::forward<Func>(func));
    } else if (entry->d_type == DT_DIR) {
      status = walk_path_dir(path, std::forward<Func>(func));
    } else if (entry->d_type == DT_REG) {
      status = walk_path_file(path, std::forward<Func>(func));
    }
#else
#warning "Slow walk_path"
    status = walk_path(path, std::forward<Func>(func));
#endif
    if (status.is_error()) {
      return status;
    }
  }
}

template <class Func>
Status walk_path_dir(string &path, DIR *subdir, Func &&func) {
  SCOPE_EXIT {
    closedir(subdir);
  };
  TRY_STATUS(walk_path_subdir(path, subdir, std::forward<Func>(func)));
  std::forward<Func>(func)(path, true);
  return Status::OK();
}

template <class Func>
Status walk_path_dir(string &path, FileFd fd, Func &&func) {
  auto *subdir = fdopendir(fd.get_fd().move_as_native_fd());
  if (subdir == nullptr) {
    auto error = OS_ERROR("fdopendir");
    fd.close();
    return error;
  }
  return walk_path_dir(path, subdir, std::forward<Func>(func));
}

template <class Func>
Status walk_path_dir(string &path, Func &&func) {
  auto *subdir = opendir(path.c_str());
  if (subdir == nullptr) {
    return OS_ERROR(PSLICE() << tag("opendir", path));
  }
  return walk_path_dir(path, subdir, std::forward<Func>(func));
}

template <class Func>
Status walk_path_file(string &path, Func &&func) {
  std::forward<Func>(func)(path, false);
  return Status::OK();
}

template <class Func>
Status walk_path(string &path, Func &&func) {
  TRY_RESULT(fd, FileFd::open(path, FileFd::Read));
  auto stat = fd.stat();
  bool is_dir = stat.is_dir_;
  bool is_reg = stat.is_reg_;
  if (is_dir) {
    return walk_path_dir(path, std::move(fd), std::forward<Func>(func));
  }

  fd.close();
  if (is_reg) {
    return walk_path_file(path, std::forward<Func>(func));
  }

  return Status::OK();
}
}  // namespace detail

template <class Func>
Status walk_path(CSlice path, Func &&func) {
  string curr_path;
  curr_path.reserve(PATH_MAX + 10);
  curr_path = path.c_str();
  return detail::walk_path(curr_path, std::forward<Func>(func));
}

#endif

#if TD_PORT_WINDOWS

namespace detail {
template <class Func>
Status walk_path_dir(const std::wstring &dir_name, Func &&func) {
  std::wstring name = dir_name + L"\\*";

  WIN32_FIND_DATA file_data;
  auto handle = FindFirstFileExW(name.c_str(), FindExInfoStandard, &file_data, FindExSearchNameMatch, nullptr, 0);
  if (handle == INVALID_HANDLE_VALUE) {
    return OS_ERROR(PSLICE() << "FindFirstFileEx" << tag("name", from_wstring(name).ok()));
  }

  SCOPE_EXIT {
    FindClose(handle);
  };
  while (true) {
    auto full_name = dir_name + L"\\" + file_data.cFileName;
    TRY_RESULT(entry_name, from_wstring(full_name));
    if (file_data.cFileName[0] != '.') {
      if ((file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        TRY_STATUS(walk_path_dir(full_name, func));
        func(entry_name, true);
      } else {
        func(entry_name, false);
      }
    }
    auto status = FindNextFileW(handle, &file_data);
    if (status == 0) {
      auto last_error = GetLastError();
      if (last_error == ERROR_NO_MORE_FILES) {
        return Status::OK();
      }
      return OS_ERROR("FindNextFileW");
    }
  }
}
}  // namespace detail

template <class Func>
Status walk_path(CSlice path, Func &&func) {
  TRY_RESULT(wpath, to_wstring(path));
  Slice path_slice = path;
  while (!path_slice.empty() && (path_slice.back() == '/' || path_slice.back() == '\\')) {
    path_slice.remove_suffix(1);
    wpath.pop_back();
  }
  return detail::walk_path_dir(wpath.c_str(), func);
}

#endif

}  // namespace td
