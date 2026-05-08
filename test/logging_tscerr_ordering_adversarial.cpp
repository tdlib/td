// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/port/config.h"
#include "td/utils/tests.h"
#include "td/utils/TsCerr.h"

#include <thread>

#if TD_PORT_POSIX
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

#if TD_PORT_POSIX

class ScopedStderrCapture final {
 public:
  bool begin() {
    char capture_template[] = "/tmp/td-tscerr-ordering-adversarial-XXXXXX";
    capture_fd_ = ::mkstemp(capture_template);
    if (capture_fd_ < 0) {
      return false;
    }
    capture_path_ = capture_template;

    saved_stderr_fd_ = ::dup(STDERR_FILENO);
    if (saved_stderr_fd_ < 0) {
      return false;
    }

    if (::dup2(capture_fd_, STDERR_FILENO) != STDERR_FILENO) {
      return false;
    }

    is_redirected_ = true;
    return true;
  }

  bool restore() {
    if (!is_redirected_) {
      return true;
    }
    if (::dup2(saved_stderr_fd_, STDERR_FILENO) != STDERR_FILENO) {
      return false;
    }
    is_redirected_ = false;
    return true;
  }

  bool read_all(td::string &output) {
    output.clear();
    if (capture_fd_ < 0) {
      return false;
    }

    if (::lseek(capture_fd_, 0, SEEK_SET) < 0) {
      return false;
    }

    char buffer[4096];
    while (true) {
      auto read_size = ::read(capture_fd_, buffer, sizeof(buffer));
      if (read_size < 0) {
        return false;
      }
      if (read_size == 0) {
        break;
      }
      output.append(buffer, static_cast<size_t>(read_size));
    }
    return true;
  }

  ~ScopedStderrCapture() {
    restore();
    if (saved_stderr_fd_ >= 0) {
      ::close(saved_stderr_fd_);
    }
    if (capture_fd_ >= 0) {
      ::close(capture_fd_);
    }
    if (!capture_path_.empty()) {
      ::unlink(capture_path_.c_str());
    }
  }

 private:
  int capture_fd_{-1};
  int saved_stderr_fd_{-1};
  bool is_redirected_{false};
  td::string capture_path_;
};

#endif

TEST(LoggingTsCerrOrderingAdversarial, ConcurrentWriteStormPreservesTaggedLineIntegrityOn14Threads) {
#if TD_PORT_POSIX
  ScopedStderrCapture capture;
  ASSERT_TRUE(capture.begin());

  constexpr int kThreads = 14;
  constexpr int kPerThread = 1800;
  const td::string marker = "tscerr-ordering-adversarial|";

  td::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int t = 0; t < kThreads; t++) {
    workers.emplace_back([t, marker] {
      for (int i = 0; i < kPerThread; i++) {
        auto line = marker + std::to_string(t) + "|" + std::to_string(i) + "\n";
        td::TsCerr() << td::Slice(line);
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  ASSERT_TRUE(capture.restore());

  td::string output;
  ASSERT_TRUE(capture.read_all(output));

  size_t marker_line_count = 0;
  size_t position = 0;
  while (position < output.size()) {
    auto newline = output.find('\n', position);
    ASSERT_TRUE(newline != td::string::npos);

    auto line = output.substr(position, newline - position);
    if (line.find(marker) == 0) {
      auto separator = line.find('|', marker.size());
      ASSERT_TRUE(separator != td::string::npos);
      marker_line_count++;
    }

    position = newline + 1;
  }

  const auto expected_lines = static_cast<size_t>(kThreads) * static_cast<size_t>(kPerThread);
  ASSERT_EQ(expected_lines, marker_line_count);
#else
  ASSERT_TRUE(true);
#endif
}

TEST(LoggingTsCerrOrderingAdversarial, SourceRejectsSeqCstReintroductionInSpinlockPaths) {
  auto normalized = normalize_for_contract(load_repo_text("tdutils/td/utils/TsCerr.cpp"));

  ASSERT_TRUE(normalized.find("std::memory_order::seq_cst") == td::string::npos);
  ASSERT_TRUE(normalized.find("test_and_set(std::memory_order_acquire)") != td::string::npos);
  ASSERT_TRUE(normalized.find("clear(std::memory_order_release)") != td::string::npos);
}

}  // namespace
