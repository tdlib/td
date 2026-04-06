//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

#include <cstdlib>

#if TD_PORT_POSIX
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

td::BufferWriter make_test_buffer(size_t size) {
  return td::BufferWriter(td::Slice(td::string(size, 'x')), 32, 0);
}

TEST(DecoratorOverflowDeathLeaf, IntentionalOverflowTriggersFatalAbort) {
  if (std::getenv("TD_STEALTH_EXPECT_OVERFLOW_FATAL") == nullptr) {
    return;
  }

  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.ring_capacity = 2;
  config.high_watermark = 1;
  config.low_watermark = 0;

  auto decorator_result = StealthTransportDecorator::create(td::make_unique<RecordingTransport>(), config,
                                                            td::make_unique<MockRng>(7), td::make_unique<MockClock>());
  CHECK(decorator_result.is_ok());
  auto decorator = decorator_result.move_as_ok();

  decorator->write(make_test_buffer(17), false);
  decorator->write(make_test_buffer(19), false);
  decorator->write(make_test_buffer(23), false);
}

#if TD_PORT_POSIX

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
    CHECK(::setenv("TD_STEALTH_EXPECT_OVERFLOW_FATAL", "1", 1) == 0);
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

TEST(DecoratorOverflowFailClosed, IntentionalOverflowCrashesChildAndEmitsInvariantMessage) {
  auto result = run_child_test("DecoratorOverflowDeathLeaf_IntentionalOverflowTriggersFatalAbort");

  ASSERT_FALSE(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 0);
  ASSERT_TRUE(result.output.find("Stealth ring overflow invariant broken") != td::string::npos);
}

#else

TEST(DecoratorOverflowFailClosed, IntentionalOverflowCrashesChildAndEmitsInvariantMessage) {
  ASSERT_TRUE(true);
}

#endif

}  // namespace