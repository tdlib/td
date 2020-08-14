//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/EventFd.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/IoSlice.h"
#include "td/utils/port/path.h"
#include "td/utils/port/signals.h"
#include "td/utils/port/sleep.h"
#include "td/utils/port/thread.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"
#include "td/utils/Time.h"

#include <atomic>
#include <set>

using namespace td;

TEST(Port, files) {
  CSlice main_dir = "test_dir";
  rmrf(main_dir).ignore();
  ASSERT_TRUE(FileFd::open(main_dir, FileFd::Write).is_error());
  ASSERT_TRUE(walk_path(main_dir, [](CSlice name, WalkPath::Type type) { UNREACHABLE(); }).is_error());
  mkdir(main_dir).ensure();
  mkdir(PSLICE() << main_dir << TD_DIR_SLASH << "A").ensure();
  mkdir(PSLICE() << main_dir << TD_DIR_SLASH << "B").ensure();
  mkdir(PSLICE() << main_dir << TD_DIR_SLASH << "B" << TD_DIR_SLASH << "D").ensure();
  mkdir(PSLICE() << main_dir << TD_DIR_SLASH << "C").ensure();
  ASSERT_TRUE(FileFd::open(main_dir, FileFd::Write).is_error());
  std::string fd_path = PSTRING() << main_dir << TD_DIR_SLASH << "t.txt";
  std::string fd2_path = PSTRING() << main_dir << TD_DIR_SLASH << "C" << TD_DIR_SLASH << "t2.txt";

  auto fd = FileFd::open(fd_path, FileFd::Write | FileFd::CreateNew).move_as_ok();
  auto fd2 = FileFd::open(fd2_path, FileFd::Write | FileFd::CreateNew).move_as_ok();
  fd2.close();

  int cnt = 0;
  const int ITER_COUNT = 1000;
  for (int i = 0; i < ITER_COUNT; i++) {
    walk_path(main_dir, [&](CSlice name, WalkPath::Type type) {
      if (type == WalkPath::Type::NotDir) {
        ASSERT_TRUE(name == fd_path || name == fd2_path);
      }
      cnt++;
    }).ensure();
  }
  ASSERT_EQ((5 * 2 + 2) * ITER_COUNT, cnt);
  bool was_abort = false;
  walk_path(main_dir, [&](CSlice name, WalkPath::Type type) {
    CHECK(!was_abort);
    if (type == WalkPath::Type::EnterDir && ends_with(name, PSLICE() << TD_DIR_SLASH << "B")) {
      was_abort = true;
      return WalkPath::Action::Abort;
    }
    return WalkPath::Action::Continue;
  }).ensure();
  CHECK(was_abort);

  cnt = 0;
  bool is_first_dir = true;
  walk_path(main_dir, [&](CSlice name, WalkPath::Type type) {
    cnt++;
    if (type == WalkPath::Type::EnterDir) {
      if (is_first_dir) {
        is_first_dir = false;
      } else {
        return WalkPath::Action::SkipDir;
      }
    }
    return WalkPath::Action::Continue;
  }).ensure();
  ASSERT_EQ(6, cnt);

  ASSERT_EQ(0u, fd.get_size().move_as_ok());
  ASSERT_EQ(12u, fd.write("Hello world!").move_as_ok());
  ASSERT_EQ(4u, fd.pwrite("abcd", 1).move_as_ok());
  char buf[100];
  MutableSlice buf_slice(buf, sizeof(buf));
  ASSERT_TRUE(fd.pread(buf_slice.substr(0, 4), 2).is_error());
  fd.seek(11).ensure();
  ASSERT_EQ(2u, fd.write("?!").move_as_ok());

  ASSERT_TRUE(FileFd::open(main_dir, FileFd::Read | FileFd::CreateNew).is_error());
  fd = FileFd::open(fd_path, FileFd::Read | FileFd::Create).move_as_ok();
  ASSERT_EQ(13u, fd.get_size().move_as_ok());
  ASSERT_EQ(4u, fd.pread(buf_slice.substr(0, 4), 1).move_as_ok());
  ASSERT_STREQ("abcd", buf_slice.substr(0, 4));

  fd.seek(0).ensure();
  ASSERT_EQ(13u, fd.read(buf_slice.substr(0, 13)).move_as_ok());
  ASSERT_STREQ("Habcd world?!", buf_slice.substr(0, 13));
}

TEST(Port, SparseFiles) {
  CSlice path = "sparse.txt";
  unlink(path).ignore();
  auto fd = FileFd::open(path, FileFd::Write | FileFd::CreateNew).move_as_ok();
  ASSERT_EQ(0, fd.get_size().move_as_ok());
  int64 offset = 100000000;
  fd.pwrite("a", offset).ensure();
  ASSERT_EQ(offset + 1, fd.get_size().move_as_ok());
  auto real_size = fd.get_real_size().move_as_ok();
  if (real_size >= offset + 1) {
    LOG(ERROR) << "File system doesn't support sparse files, rewind during streaming can be slow";
  }
  unlink(path).ensure();
}

TEST(Port, Writev) {
  std::vector<IoSlice> vec;
  CSlice test_file_path = "test.txt";
  unlink(test_file_path).ignore();
  auto fd = FileFd::open(test_file_path, FileFd::Write | FileFd::CreateNew).move_as_ok();
  vec.push_back(as_io_slice("a"));
  vec.push_back(as_io_slice("b"));
  vec.push_back(as_io_slice("cd"));
  ASSERT_EQ(4u, fd.writev(vec).move_as_ok());
  vec.clear();
  vec.push_back(as_io_slice("efg"));
  vec.push_back(as_io_slice(""));
  vec.push_back(as_io_slice("hi"));
  ASSERT_EQ(5u, fd.writev(vec).move_as_ok());
  fd.close();
  fd = FileFd::open(test_file_path, FileFd::Read).move_as_ok();
  Slice expected_content = "abcdefghi";
  ASSERT_EQ(static_cast<int64>(expected_content.size()), fd.get_size().ok());
  std::string content(expected_content.size(), '\0');
  ASSERT_EQ(content.size(), fd.read(content).move_as_ok());
  ASSERT_EQ(expected_content, content);
}

#if TD_PORT_POSIX && !TD_THREAD_UNSUPPORTED
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>

static std::mutex m;
static std::vector<std::string> ptrs;
static std::vector<int *> addrs;
static TD_THREAD_LOCAL int thread_id;

static void on_user_signal(int sig) {
  int addr;
  addrs[thread_id] = &addr;
  char ptr[10];
  snprintf(ptr, 6, "%d", thread_id);
  std::unique_lock<std::mutex> guard(m);
  ptrs.push_back(std::string(ptr));
}

TEST(Port, SignalsAndThread) {
  setup_signals_alt_stack().ensure();
  set_signal_handler(SignalType::User, on_user_signal).ensure();
  SCOPE_EXIT {
    set_signal_handler(SignalType::User, nullptr).ensure();
  };
  std::vector<std::string> ans = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
  {
    std::vector<td::thread> threads;
    int thread_n = 10;
    std::vector<Stage> stages(thread_n);
    ptrs.clear();
    addrs.resize(thread_n);
    for (int i = 0; i < 10; i++) {
      threads.emplace_back([&, i] {
        setup_signals_alt_stack().ensure();
        if (i != 0) {
          stages[i].wait(2);
        }
        thread_id = i;
        pthread_kill(pthread_self(), SIGUSR1);
        if (i + 1 < thread_n) {
          stages[i + 1].wait(2);
        }
      });
    }
    for (auto &t : threads) {
      t.join();
    }
    CHECK(ptrs == ans);

    //LOG(ERROR) << ptrs;
    //LOG(ERROR) << std::set<int *>(addrs.begin(), addrs.end()).size();
    //LOG(ERROR) << addrs;
  }

  {
    Stage stage;
    std::vector<td::thread> threads;
    int thread_n = 10;
    ptrs.clear();
    addrs.resize(thread_n);
    for (int i = 0; i < 10; i++) {
      threads.emplace_back([&, i] {
        stage.wait(thread_n);
        thread_id = i;
        pthread_kill(pthread_self(), SIGUSR1);
        //kill(pid_t(syscall(SYS_gettid)), SIGUSR1);
      });
    }
    for (auto &t : threads) {
      t.join();
    }
    std::sort(ptrs.begin(), ptrs.end());
    CHECK(ptrs == ans);
    std::sort(addrs.begin(), addrs.end());
    ASSERT_TRUE(std::unique(addrs.begin(), addrs.end()) == addrs.end());
    //LOG(ERROR) << addrs;
  }
}

TEST(Port, EventFdAndSignals) {
  set_signal_handler(SignalType::User, [](int signal) {}).ensure();
  SCOPE_EXIT {
    set_signal_handler(SignalType::User, nullptr).ensure();
  };

  std::atomic_flag flag;
  flag.test_and_set();
  auto main_thread = pthread_self();
  td::thread interrupt_thread{[&flag, &main_thread] {
    setup_signals_alt_stack().ensure();
    while (flag.test_and_set()) {
      pthread_kill(main_thread, SIGUSR1);
      td::usleep_for(1000 * td::Random::fast(1, 10));  // 0.001s - 0.01s
    }
  }};

  for (int timeout_ms : {0, 1, 2, 10, 100, 500}) {
    double min_diff = 10000000;
    double max_diff = 0;
    for (int t = 0; t < max(5, 1000 / td::max(timeout_ms, 1)); t++) {
      td::EventFd event_fd;
      event_fd.init();
      auto start = td::Timestamp::now();
      event_fd.wait(timeout_ms);
      auto end = td::Timestamp::now();
      auto passed = end.at() - start.at();
      auto diff = passed * 1000 - timeout_ms;
      min_diff = td::min(min_diff, diff);
      max_diff = td::max(max_diff, diff);
    }

    LOG_CHECK(min_diff >= 0) << min_diff;
    LOG_CHECK(max_diff < 10) << max_diff;
    LOG(INFO) << min_diff << " " << max_diff;
  }
  flag.clear();
}
#endif
