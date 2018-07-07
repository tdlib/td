//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/path.h"

#include "td/utils/port/Fd.h"

#if TD_WINDOWS
#include "td/utils/Random.h"
#endif

#if TD_PORT_POSIX

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

#include <cstdlib>

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

#if TD_PORT_POSIX

Status mkdir(CSlice dir, int32 mode) {
  int mkdir_res = skip_eintr([&] { return ::mkdir(dir.c_str(), static_cast<mode_t>(mode)); });
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
  int rename_res = skip_eintr([&] { return ::rename(from.c_str(), to.c_str()); });
  if (rename_res < 0) {
    return OS_ERROR(PSLICE() << "Can't rename \"" << from << "\" to \"" << to << '\"');
  }
  return Status::OK();
}

Result<string> realpath(CSlice slice, bool ignore_access_denied) {
  char full_path[PATH_MAX + 1];
  string res;
  char *err = skip_eintr_cstr([&] { return ::realpath(slice.c_str(), full_path); });
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
  int chdir_res = skip_eintr([&] { return ::chdir(dir.c_str()); });
  if (chdir_res) {
    return OS_ERROR(PSLICE() << "Can't change directory to \"" << dir << '"');
  }
  return Status::OK();
}

Status rmdir(CSlice dir) {
  int rmdir_res = skip_eintr([&] { return ::rmdir(dir.c_str()); });
  if (rmdir_res) {
    return OS_ERROR(PSLICE() << "Can't delete directory \"" << dir << '"');
  }
  return Status::OK();
}

Status unlink(CSlice path) {
  int unlink_res = skip_eintr([&] { return ::unlink(path.c_str()); });
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

  int fd = skip_eintr([&] { return ::mkstemp(&file_pattern[0]); });
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

  char *result = skip_eintr_cstr([&] { return ::mkdtemp(&dir_pattern[0]); });
  if (result == nullptr) {
    return OS_ERROR(PSLICE() << "Can't create temporary directory \"" << dir_pattern << '"');
  }
  return result;
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

#endif

}  // namespace td
