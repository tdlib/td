//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/path.h"

#include "td/utils/port/config.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/ScopeGuard.h"

#if TD_PORT_WINDOWS
#include "td/utils/port/wstring_convert.h"
#include "td/utils/Random.h"
#endif

#if TD_PORT_POSIX

#include <dirent.h>
#include <limits.h>
#include <stdio.h>

// We don't want warnings from system headers
#if TD_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include <sys/stat.h>
#if TD_GCC
#pragma GCC diagnostic pop
#endif

#include <sys/types.h>
#include <unistd.h>

#endif

#if TD_DARWIN
#include <sys/syslimits.h>
#endif

#include <cstdlib>
#include <string>

namespace td {

static string temporary_dir;

Status set_temporary_dir(CSlice dir) {
  string input_dir = dir.str();
  if (!dir.empty() && dir.back() != TD_DIR_SLASH) {
    input_dir += TD_DIR_SLASH;
  }
  TRY_STATUS(mkpath(input_dir, 0750));
  TRY_RESULT(real_dir, realpath(input_dir));
  temporary_dir = std::move(real_dir);
  return Status::OK();
}

Status mkpath(CSlice path, int32 mode) {
  Status first_error = Status::OK();
  Status last_error = Status::OK();
  for (size_t i = 1; i < path.size(); i++) {
    if (path[i] == TD_DIR_SLASH) {
      last_error = mkdir(PSLICE() << path.substr(0, i), mode);
      if (last_error.is_error() && first_error.is_ok()) {
        first_error = last_error.clone();
      }
    }
  }
  if (last_error.is_error()) {
    return first_error;
  }
  return Status::OK();
}

Status rmrf(CSlice path) {
  return walk_path(path, [](CSlice path, bool is_dir) {
    if (is_dir) {
      rmdir(path).ignore();
    } else {
      unlink(path).ignore();
    }
  });
}

#if TD_PORT_POSIX

Status mkdir(CSlice dir, int32 mode) {
  int mkdir_res = [&] {
    int res;
    do {
      errno = 0;  // just in case
      res = ::mkdir(dir.c_str(), static_cast<mode_t>(mode));
    } while (res < 0 && (errno == EINTR || errno == EAGAIN));
    return res;
  }();
  if (mkdir_res == 0) {
    return Status::OK();
  }
  auto mkdir_errno = errno;
  if (mkdir_errno == EEXIST) {
    // TODO check that it is a directory
    return Status::OK();
  }
  return Status::PosixError(mkdir_errno, PSLICE() << "Can't create directory \"" << dir << '"');
}

Status rename(CSlice from, CSlice to) {
  int rename_res = detail::skip_eintr([&] { return ::rename(from.c_str(), to.c_str()); });
  if (rename_res < 0) {
    return OS_ERROR(PSLICE() << "Can't rename \"" << from << "\" to \"" << to << '\"');
  }
  return Status::OK();
}

Result<string> realpath(CSlice slice, bool ignore_access_denied) {
  char full_path[PATH_MAX + 1];
  string res;
  char *err = detail::skip_eintr_cstr([&] { return ::realpath(slice.c_str(), full_path); });
  if (err != full_path) {
    if (ignore_access_denied && (errno == EACCES || errno == EPERM)) {
      res = slice.str();
    } else {
      return OS_ERROR(PSLICE() << "Realpath failed for \"" << slice << '"');
    }
  } else {
    res = full_path;
  }
  if (res.empty()) {
    return Status::Error("Empty path");
  }
  if (!slice.empty() && slice.end()[-1] == TD_DIR_SLASH) {
    if (res.back() != TD_DIR_SLASH) {
      res += TD_DIR_SLASH;
    }
  }
  return res;
}

Status chdir(CSlice dir) {
  int chdir_res = detail::skip_eintr([&] { return ::chdir(dir.c_str()); });
  if (chdir_res) {
    return OS_ERROR(PSLICE() << "Can't change directory to \"" << dir << '"');
  }
  return Status::OK();
}

Status rmdir(CSlice dir) {
  int rmdir_res = detail::skip_eintr([&] { return ::rmdir(dir.c_str()); });
  if (rmdir_res) {
    return OS_ERROR(PSLICE() << "Can't delete directory \"" << dir << '"');
  }
  return Status::OK();
}

Status unlink(CSlice path) {
  int unlink_res = detail::skip_eintr([&] { return ::unlink(path.c_str()); });
  if (unlink_res) {
    return OS_ERROR(PSLICE() << "Can't unlink \"" << path << '"');
  }
  return Status::OK();
}

CSlice get_temporary_dir() {
  static bool is_inited = [] {
    if (temporary_dir.empty()) {
      const char *s = std::getenv("TMPDIR");
      if (s != nullptr && s[0] != '\0') {
        temporary_dir = s;
      } else if (P_tmpdir != nullptr && P_tmpdir[0] != '\0') {
        temporary_dir = P_tmpdir;
      } else {
        return false;
      }
    }
    if (temporary_dir.size() > 1 && temporary_dir.back() == TD_DIR_SLASH) {
      temporary_dir.pop_back();
    }
    return true;
  }();
  LOG_IF(FATAL, !is_inited) << "Can't find temporary directory";
  return temporary_dir;
}

Result<std::pair<FileFd, string>> mkstemp(CSlice dir) {
  if (dir.empty()) {
    dir = get_temporary_dir();
    if (dir.empty()) {
      return Status::Error("Can't find temporary directory");
    }
  }

  TRY_RESULT(dir_real, realpath(dir));
  CHECK(!dir_real.empty());

  string file_pattern;
  file_pattern.reserve(dir_real.size() + 14);
  file_pattern = dir_real;
  if (file_pattern.back() != TD_DIR_SLASH) {
    file_pattern += TD_DIR_SLASH;
  }
  file_pattern += "tmpXXXXXXXXXX";

  int fd = detail::skip_eintr([&] { return ::mkstemp(&file_pattern[0]); });
  if (fd == -1) {
    return OS_ERROR(PSLICE() << "Can't create temporary file \"" << file_pattern << '"');
  }
  if (close(fd)) {
    return OS_ERROR(PSLICE() << "Can't close temporary file \"" << file_pattern << '"');
  }
  // TODO create file from fd
  TRY_RESULT(file, FileFd::open(file_pattern, FileFd::Write | FileFd::Truncate | FileFd::Append));
  return std::make_pair(std::move(file), std::move(file_pattern));
}

Result<string> mkdtemp(CSlice dir, Slice prefix) {
  if (dir.empty()) {
    dir = get_temporary_dir();
    if (dir.empty()) {
      return Status::Error("Can't find temporary directory");
    }
  }

  TRY_RESULT(dir_real, realpath(dir));
  CHECK(!dir_real.empty());

  string dir_pattern;
  dir_pattern.reserve(dir_real.size() + prefix.size() + 7);
  dir_pattern = dir_real;
  if (dir_pattern.back() != TD_DIR_SLASH) {
    dir_pattern += TD_DIR_SLASH;
  }
  dir_pattern.append(prefix.begin(), prefix.size());
  dir_pattern += "XXXXXX";

  char *result = detail::skip_eintr_cstr([&] { return ::mkdtemp(&dir_pattern[0]); });
  if (result == nullptr) {
    return OS_ERROR(PSLICE() << "Can't create temporary directory \"" << dir_pattern << '"');
  }
  return result;
}

namespace detail {
Status walk_path_dir(string &path, FileFd fd,
                     const std::function<void(CSlice name, bool is_directory)> &func) TD_WARN_UNUSED_RESULT;

Status walk_path_dir(string &path,
                     const std::function<void(CSlice name, bool is_directory)> &func) TD_WARN_UNUSED_RESULT;

Status walk_path_file(string &path,
                      const std::function<void(CSlice name, bool is_directory)> &func) TD_WARN_UNUSED_RESULT;

Status walk_path(string &path, const std::function<void(CSlice name, bool is_directory)> &func) TD_WARN_UNUSED_RESULT;

Status walk_path_subdir(string &path, DIR *dir, const std::function<void(CSlice name, bool is_directory)> &func) {
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
    Slice name = Slice(static_cast<const char *>(entry->d_name));
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
      status = walk_path(path, func);
    } else if (entry->d_type == DT_DIR) {
      status = walk_path_dir(path, func);
    } else if (entry->d_type == DT_REG) {
      status = walk_path_file(path, func);
    }
#else
#warning "Slow walk_path"
    status = walk_path(path, func);
#endif
    if (status.is_error()) {
      return status;
    }
  }
}

Status walk_path_dir(string &path, DIR *subdir, const std::function<void(CSlice name, bool is_directory)> &func) {
  SCOPE_EXIT {
    closedir(subdir);
  };
  TRY_STATUS(walk_path_subdir(path, subdir, func));
  func(path, true);
  return Status::OK();
}

Status walk_path_dir(string &path, FileFd fd, const std::function<void(CSlice name, bool is_directory)> &func) {
  auto native_fd = fd.move_as_native_fd();
  auto *subdir = fdopendir(native_fd.fd());
  if (subdir == nullptr) {
    return OS_ERROR("fdopendir");
  }
  native_fd.release();
  return walk_path_dir(path, subdir, func);
}

Status walk_path_dir(string &path, const std::function<void(CSlice name, bool is_directory)> &func) {
  auto *subdir = opendir(path.c_str());
  if (subdir == nullptr) {
    return OS_ERROR(PSLICE() << tag("opendir", path));
  }
  return walk_path_dir(path, subdir, func);
}

Status walk_path_file(string &path, const std::function<void(CSlice name, bool is_directory)> &func) {
  func(path, false);
  return Status::OK();
}

Status walk_path(string &path, const std::function<void(CSlice name, bool is_directory)> &func) {
  TRY_RESULT(fd, FileFd::open(path, FileFd::Read));
  auto stat = fd.stat();
  bool is_dir = stat.is_dir_;
  bool is_reg = stat.is_reg_;
  if (is_dir) {
    return walk_path_dir(path, std::move(fd), func);
  }

  fd.close();
  if (is_reg) {
    return walk_path_file(path, func);
  }

  return Status::OK();
}
}  // namespace detail

Status walk_path(CSlice path, const std::function<void(CSlice name, bool is_directory)> &func) {
  string curr_path;
  curr_path.reserve(PATH_MAX + 10);
  curr_path = path.c_str();
  return detail::walk_path(curr_path, func);
}

#endif

#if TD_PORT_WINDOWS

Status mkdir(CSlice dir, int32 mode) {
  TRY_RESULT(wdir, to_wstring(dir));
  auto status = CreateDirectoryW(wdir.c_str(), nullptr);
  if (status == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
    return OS_ERROR(PSLICE() << "Can't create directory \"" << dir << '"');
  }
  return Status::OK();
}

Status rename(CSlice from, CSlice to) {
  TRY_RESULT(wfrom, to_wstring(from));
  TRY_RESULT(wto, to_wstring(to));
  auto status = MoveFileExW(wfrom.c_str(), wto.c_str(), MOVEFILE_REPLACE_EXISTING);
  if (status == 0) {
    return OS_ERROR(PSLICE() << "Can't rename \"" << from << "\" to \"" << to << '\"');
  }
  return Status::OK();
}

Result<string> realpath(CSlice slice, bool ignore_access_denied) {
  wchar_t buf[MAX_PATH + 1];
  TRY_RESULT(wslice, to_wstring(slice));
  auto status = GetFullPathNameW(wslice.c_str(), MAX_PATH, buf, nullptr);
  string res;
  if (status == 0) {
    if (ignore_access_denied && errno == ERROR_ACCESS_DENIED) {
      res = slice.str();
    } else {
      return OS_ERROR(PSLICE() << "GetFullPathNameW failed for \"" << slice << '"');
    }
  } else {
    TRY_RESULT(t_res, from_wstring(buf));
    res = std::move(t_res);
  }
  if (res.empty()) {
    return Status::Error("Empty path");
  }
  if (!slice.empty() && slice.end()[-1] == TD_DIR_SLASH) {
    if (res.back() != TD_DIR_SLASH) {
      res += TD_DIR_SLASH;
    }
  }
  return res;
}

Status chdir(CSlice dir) {
  TRY_RESULT(wdir, to_wstring(dir));
  auto res = SetCurrentDirectoryW(wdir.c_str());
  if (res == 0) {
    return OS_ERROR(PSLICE() << "Can't change directory to \"" << dir << '"');
  }
  return Status::OK();
}

Status rmdir(CSlice dir) {
  TRY_RESULT(wdir, to_wstring(dir));
  int status = RemoveDirectoryW(wdir.c_str());
  if (!status) {
    return OS_ERROR(PSLICE() << "Can't delete directory \"" << dir << '"');
  }
  return Status::OK();
}

Status unlink(CSlice path) {
  TRY_RESULT(wpath, to_wstring(path));
  int status = DeleteFileW(wpath.c_str());
  if (!status) {
    return OS_ERROR(PSLICE() << "Can't unlink \"" << path << '"');
  }
  return Status::OK();
}

CSlice get_temporary_dir() {
  static bool is_inited = [] {
    if (temporary_dir.empty()) {
      wchar_t buf[MAX_PATH + 1];
      if (GetTempPathW(MAX_PATH, buf) == 0) {
        auto error = OS_ERROR("GetTempPathW failed");
        LOG(FATAL) << error;
      }
      auto rs = from_wstring(buf);
      LOG_IF(FATAL, rs.is_error()) << "GetTempPathW failed: " << rs.error();
      temporary_dir = rs.ok();
    }
    if (temporary_dir.size() > 1 && temporary_dir.back() == TD_DIR_SLASH) {
      temporary_dir.pop_back();
    }
    return true;
  }();
  LOG_IF(FATAL, !is_inited) << "Can't find temporary directory";
  return temporary_dir;
}

Result<string> mkdtemp(CSlice dir, Slice prefix) {
  if (dir.empty()) {
    dir = get_temporary_dir();
    if (dir.empty()) {
      return Status::Error("Can't find temporary directory");
    }
  }

  TRY_RESULT(dir_real, realpath(dir));
  CHECK(!dir_real.empty());

  string dir_pattern;
  dir_pattern.reserve(dir_real.size() + prefix.size() + 7);
  dir_pattern = dir_real;
  if (dir_pattern.back() != TD_DIR_SLASH) {
    dir_pattern += TD_DIR_SLASH;
  }
  dir_pattern.append(prefix.begin(), prefix.size());

  for (auto iter = 0; iter < 20; iter++) {
    auto path = dir_pattern;
    for (int i = 0; i < 6 + iter / 5; i++) {
      path += static_cast<char>(Random::fast('a', 'z'));
    }
    auto status = mkdir(path);
    if (status.is_ok()) {
      return path;
    }
  }
  return Status::Error(PSLICE() << "Can't create temporary directory \"" << dir_pattern << '"');
}

Result<std::pair<FileFd, string>> mkstemp(CSlice dir) {
  if (dir.empty()) {
    dir = get_temporary_dir();
    if (dir.empty()) {
      return Status::Error("Can't find temporary directory");
    }
  }

  TRY_RESULT(dir_real, realpath(dir));
  CHECK(!dir_real.empty());

  string file_pattern;
  file_pattern.reserve(dir_real.size() + 14);
  file_pattern = dir_real;
  if (file_pattern.back() != TD_DIR_SLASH) {
    file_pattern += TD_DIR_SLASH;
  }
  file_pattern += "tmp";

  for (auto iter = 0; iter < 20; iter++) {
    auto path = file_pattern;
    for (int i = 0; i < 6 + iter / 5; i++) {
      path += static_cast<char>(Random::fast('a', 'z'));
    }
    auto r_file = FileFd::open(path, FileFd::Write | FileFd::Read | FileFd::CreateNew);
    if (r_file.is_ok()) {
      return std::make_pair(r_file.move_as_ok(), path);
    }
  }

  return Status::Error(PSLICE() << "Can't create temporary file \"" << file_pattern << '"');
}

static Status walk_path_dir(const std::wstring &dir_name,
                            const std::function<void(CSlice name, bool is_directory)> &func) {
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
      } else {
        func(entry_name, false);
      }
    }
    auto status = FindNextFileW(handle, &file_data);
    if (status == 0) {
      auto last_error = GetLastError();
      if (last_error == ERROR_NO_MORE_FILES) {
        break;
      }
      return OS_ERROR("FindNextFileW");
    }
  }
  TRY_RESULT(entry_name, from_wstring(dir_name));
  func(entry_name, true);
  return Status::OK();
}

Status walk_path(CSlice path, const std::function<void(CSlice name, bool is_directory)> &func) {
  TRY_RESULT(wpath, to_wstring(path));
  Slice path_slice = path;
  while (!path_slice.empty() && (path_slice.back() == '/' || path_slice.back() == '\\')) {
    path_slice.remove_suffix(1);
    wpath.pop_back();
  }
  return walk_path_dir(wpath, func);
}

#endif

}  // namespace td
