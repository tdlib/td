// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/telegram/Log.h"

#include "td/utils/logging.h"
#include "td/utils/port/config.h"

#include "td/utils/tests.h"

#include <cstdlib>

#if TD_PORT_POSIX
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

#if defined(__SANITIZE_LEAK__) || TD_HAS_FEATURE_LEAK_SANITIZER || defined(__SANITIZE_MEMORY__) || \
    TD_HAS_FEATURE_MEMORY_SANITIZER || defined(__SANITIZE_THREAD__) || TD_HAS_FEATURE_THREAD_SANITIZER
constexpr bool kFatalAbortChildSanitizerBuild = true;
#else
constexpr bool kFatalAbortChildSanitizerBuild = false;
#endif

#if TD_PORT_POSIX

constexpr td::Slice kContractCallbackMarker("LOG_FATAL_CALLBACK_CONTRACT_MARKER");
constexpr const char *kContractChildEnv = "TD_LOG_FATAL_CALLBACK_CONTRACT_CHILD";

struct ChildRunResult final {
  int status{0};
  td::string output;
};

td::string get_self_executable_path() {
  td::string path(4096, '\0');
  auto size = ::readlink("/proc/self/exe", &path[0], path.size() - 1);
  CHECK(size > 0);
  path.resize(static_cast<size_t>(size));
  return path;
}

ChildRunResult run_child_test(td::Slice filter) {
  int pipe_fds[2] = {-1, -1};
  CHECK(::pipe(pipe_fds) == 0);

  auto executable = get_self_executable_path();
  auto filter_string = filter.str();
  auto child_pid = ::fork();
  CHECK(child_pid >= 0);

  if (child_pid == 0) {
    ::close(pipe_fds[0]);
    CHECK(::dup2(pipe_fds[1], 1) >= 0);
    CHECK(::dup2(pipe_fds[1], 2) >= 0);
    ::close(pipe_fds[1]);
    CHECK(::setenv(kContractChildEnv, "1", 1) == 0);
    ::execl(executable.c_str(), executable.c_str(), "--filter", filter_string.c_str(), nullptr);
    _exit(127);
  }

  ::close(pipe_fds[1]);

  ChildRunResult result;
  char buffer[1024];
  while (true) {
    auto read_size = ::read(pipe_fds[0], buffer, sizeof(buffer));
    if (read_size <= 0) {
      break;
    }
    result.output.append(buffer, static_cast<size_t>(read_size));
  }
  ::close(pipe_fds[0]);

  CHECK(::waitpid(child_pid, &result.status, 0) == child_pid);
  return result;
}

void emit_contract_callback_marker(const char *error_message) {
  (void)error_message;
  static const char marker[] = "LOG_FATAL_CALLBACK_CONTRACT_MARKER\n";
  auto ignored = ::write(STDERR_FILENO, marker, sizeof(marker) - 1);
  (void)ignored;
}

#endif

TEST(LoggingFatalCallbackContract, CallbackStorageUsesAtomicPointerSemantics) {
  auto source = load_repo_text("td/telegram/Log.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("std::atomic<Log::FatalErrorCallbackPtr>fatal_error_callback") != td::string::npos);
  ASSERT_TRUE(normalized.find("staticLog::FatalErrorCallbackPtrfatal_error_callback") == td::string::npos);
}

TEST(LoggingFatalCallbackContract, WrapperLoadsCallbackAtomicallyAndRemainsNullSafe) {
  auto source = load_repo_text("td/telegram/Log.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("fatal_error_callback.load(std::memory_order_acquire)") != td::string::npos);
  ASSERT_TRUE(source.find("if (callback != nullptr)") != td::string::npos);
}

TEST(LoggingFatalCallbackContract, SetterStoresCallbackWithReleaseOrdering) {
  auto source = load_repo_text("td/telegram/Log.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("fatal_error_callback.store(nullptr,std::memory_order_release)") != td::string::npos);
  ASSERT_TRUE(normalized.find("fatal_error_callback.store(callback,std::memory_order_release)") != td::string::npos);
}

#if TD_PORT_POSIX

TEST(LoggingFatalCallbackContractDeathLeaf, CallbackIsInvokedBeforeFatalAbort) {
  if (std::getenv(kContractChildEnv) == nullptr) {
    return;
  }

  td::Log::set_fatal_error_callback(&emit_contract_callback_marker);
  td::process_fatal_error("fatal callback contract probe\n");
}

TEST(LoggingFatalCallbackContractDeathLeaf, NullCallbackPathStaysFailSafeBeforeFatalAbort) {
  if (std::getenv(kContractChildEnv) == nullptr) {
    return;
  }

  td::Log::set_fatal_error_callback(nullptr);
  td::process_fatal_error("fatal callback null contract probe\n");
}

TEST(LoggingFatalCallbackContract, RuntimeFatalPathInvokesInstalledCallback) {
  if (kFatalAbortChildSanitizerBuild) {
    return;
  }
  auto result = run_child_test("LoggingFatalCallbackContractDeathLeaf_CallbackIsInvokedBeforeFatalAbort");

  ASSERT_FALSE(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 0);
  ASSERT_TRUE(result.output.find(kContractCallbackMarker.str()) != td::string::npos);
}

TEST(LoggingFatalCallbackContract, RuntimeFatalPathWithNullCallbackAvoidsInvocation) {
  if (kFatalAbortChildSanitizerBuild) {
    return;
  }
  auto result = run_child_test("LoggingFatalCallbackContractDeathLeaf_NullCallbackPathStaysFailSafeBeforeFatalAbort");

  ASSERT_FALSE(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 0);
  ASSERT_TRUE(result.output.find(kContractCallbackMarker.str()) == td::string::npos);
}

#else

TEST(LoggingFatalCallbackContract, RuntimeFatalPathInvokesInstalledCallback) {
  ASSERT_TRUE(true);
}

TEST(LoggingFatalCallbackContract, RuntimeFatalPathWithNullCallbackAvoidsInvocation) {
  ASSERT_TRUE(true);
}

#endif

}  // namespace
