//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/FileLog.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/port/StdStreams.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Time.h"

namespace td {

namespace {

string format_fatal_log_error(const Status &status) {
  return PSTRING() << status << '\n';
}

}  // namespace

Status FileLog::init(string path, int64 rotate_threshold, bool redirect_stderr) {
  auto lock = mutex_.lock();
  if (path.empty()) {
    return Status::Error("Log file path must be non-empty");
  }
  if (path == path_) {
    rotate_threshold_ = rotate_threshold;
    return Status::OK();
  }

  TRY_RESULT(fd, FileFd::open(path, FileFd::Create | FileFd::Write | FileFd::Append));

  fd_.close();
  fd_ = std::move(fd);
  if (!Stderr().empty() && redirect_stderr) {
    fd_.get_native_fd().duplicate(Stderr().get_native_fd()).ignore();
  }

  if (auto r_path = realpath(path, true); r_path.is_error()) {
    path_ = std::move(path);
  } else {
    path_ = r_path.move_as_ok();
  }
  TRY_RESULT_ASSIGN(size_, fd_.get_size());
  rotate_threshold_ = rotate_threshold;
  redirect_stderr_ = redirect_stderr;
  return Status::OK();
}

string FileLog::get_path() const {
  auto lock = mutex_.lock();
  return path_;
}

vector<string> FileLog::get_file_paths() {
  auto lock = mutex_.lock();
  vector<string> result;
  if (!path_.empty()) {
    result.push_back(path_);
    string old_path = PSTRING() << path_ << ".old";
    result.push_back(std::move(old_path));
  }
  return result;
}

void FileLog::set_rotate_threshold(int64 rotate_threshold) {
  auto lock = mutex_.lock();
  rotate_threshold_ = rotate_threshold;
}

int64 FileLog::get_rotate_threshold() const {
  auto lock = mutex_.lock();
  return rotate_threshold_;
}

bool FileLog::get_redirect_stderr() const {
  auto lock = mutex_.lock();
  return redirect_stderr_;
}

void FileLog::do_append(int log_level, CSlice slice) {
  string fatal_error;

  auto lock = mutex_.lock();
  auto start_time = Time::now();
  if (size_ > rotate_threshold_ || want_rotate_.load()) {
    if (auto status = rename(path_, PSLICE() << path_ << ".old"); status.is_error()) {
      fatal_error = format_fatal_log_error(status);
    } else {
      if (auto reopen_status = do_after_rotation_locked(); reopen_status.is_error()) {
        fatal_error = format_fatal_log_error(reopen_status);
      }
    }
  }
  while (fatal_error.empty() && !slice.empty()) {
    if (redirect_stderr_) {
      while (has_log_guard()) {
        // spin
      }
    }
    auto r_size = fd_.write(slice);
    if (r_size.is_error()) {
      fatal_error = format_fatal_log_error(r_size.error());
      break;
    }
    auto written = r_size.ok();
    size_ += static_cast<int64>(written);
    slice.remove_prefix(written);
  }
  if (auto total_time = Time::now() - start_time; fatal_error.empty() && total_time >= 0.1 && log_level >= 1) {
    auto thread_id = get_thread_id();
    auto r_size = fd_.write(PSLICE() << "[ 2][t" << (0 <= thread_id && thread_id < 10 ? " " : "") << thread_id
                                     << "] !!! Previous logging took " << total_time << " seconds !!!\n");
    r_size.ignore();
  }
  lock.reset();

  if (!fatal_error.empty()) {
    process_fatal_error(fatal_error);
  }
}

void FileLog::after_rotation() {
  string fatal_error;

  auto lock = mutex_.lock();
  if (path_.empty()) {
    return;
  }
  if (auto status = do_after_rotation_locked(); status.is_error()) {
    fatal_error = format_fatal_log_error(status);
  }
  lock.reset();

  if (!fatal_error.empty()) {
    process_fatal_error(fatal_error);
  }
}

void FileLog::lazy_rotate() {
  want_rotate_ = true;
}

Status FileLog::do_after_rotation_locked() {
  want_rotate_ = false;
  ScopedDisableLog disable_log;  // to ensure that nothing will be printed to the closed log
  if (path_.empty()) {
    return Status::Error("Log file path must be non-empty");
  }
  fd_.close();
  TRY_RESULT(fd, FileFd::open(path_, FileFd::Create | FileFd::Write | FileFd::Append));
  fd_ = std::move(fd);
  if (!Stderr().empty() && redirect_stderr_) {
    fd_.get_native_fd().duplicate(Stderr().get_native_fd()).ignore();
  }
  TRY_RESULT_ASSIGN(size_, fd_.get_size());
  return Status::OK();
}

Result<unique_ptr<LogInterface>> FileLog::create(string path, int64 rotate_threshold, bool redirect_stderr) {
  auto l = make_unique<FileLog>();
  TRY_STATUS(l->init(std::move(path), rotate_threshold, redirect_stderr));
  return std::move(l);
}

}  // namespace td
