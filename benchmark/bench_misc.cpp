//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/benchmark.h"
#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/port/EventFd.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/port/RwMutex.h"
#include "td/utils/port/Stat.h"
#include "td/utils/port/thread.h"
#include "td/utils/Slice.h"

#include "td/telegram/telegram_api.h"
#include "td/telegram/telegram_api.hpp"

#if !TD_WINDOWS
#include <unistd.h>
#include <utime.h>
#endif

#if TD_LINUX || TD_ANDROID || TD_TIZEN
#include <semaphore.h>
#endif

#include <atomic>
#include <cstdint>

namespace td {

class F {
  uint32 &sum;

 public:
  explicit F(uint32 &sum) : sum(sum) {
  }

  template <class T>
  void operator()(const T &x) const {
    sum += static_cast<uint32>(x.get_id());
  }
};

BENCH(Call, "TL Call") {
  tl_object_ptr<telegram_api::Function> x = make_tl_object<telegram_api::account_getWallPapers>(0);
  uint32 res = 0;
  F f(res);
  for (int i = 0; i < n; i++) {
    downcast_call(*x, f);
  }
  do_not_optimize_away(res);
}

#if !TD_EVENTFD_UNSUPPORTED
BENCH(EventFd, "EventFd") {
  EventFd fd;
  fd.init();
  for (int i = 0; i < n; i++) {
    fd.release();
    fd.acquire();
  }
  fd.close();
}
#endif

BENCH(NewInt, "new int + delete") {
  std::uintptr_t res = 0;
  for (int i = 0; i < n; i++) {
    int *x = new int;
    res += reinterpret_cast<std::uintptr_t>(x);
    delete x;
  }
  do_not_optimize_away(res);
}

BENCH(NewObj, "new struct, then delete") {
  struct A {
    int32 a = 0;
    int32 b = 0;
    int32 c = 0;
    int32 d = 0;
  };
  std::uintptr_t res = 0;
  A **ptr = new A *[n];
  for (int i = 0; i < n; i++) {
    ptr[i] = new A();
    res += reinterpret_cast<std::uintptr_t>(ptr[i]);
  }
  for (int i = 0; i < n; i++) {
    delete ptr[i];
  }
  delete[] ptr;
  do_not_optimize_away(res);
}

#if !TD_THREAD_UNSUPPORTED
BENCH(ThreadNew, "new struct, then delete in several threads") {
  td::NewObjBench a, b;
  thread ta([&] { a.run(n / 2); });
  thread tb([&] { b.run(n - n / 2); });
  ta.join();
  tb.join();
}
#endif
/*
// Too hard for clang (?)
BENCH(Time, "Clocks::monotonic") {
  double res = 0;
  for (int i = 0; i < n; i++) {
    res += Clocks::monotonic();
  }
  do_not_optimize_away(res);
}
*/
#if !TD_WINDOWS
class PipeBench : public Benchmark {
 public:
  int p[2];

  string get_description() const override {
    return "pipe write + read int32";
  }

  void start_up() override {
    int res = pipe(p);
    CHECK(res == 0);
  }

  void run(int n) override {
    int res = 0;
    for (int i = 0; i < n; i++) {
      int val = 1;
      auto write_len = write(p[1], &val, sizeof(val));
      CHECK(write_len == sizeof(val));
      auto read_len = read(p[0], &val, sizeof(val));
      CHECK(read_len == sizeof(val));
      res += val;
    }
    do_not_optimize_away(res);
  }

  void tear_down() override {
    close(p[0]);
    close(p[1]);
  }
};
#endif

#if TD_LINUX || TD_ANDROID || TD_TIZEN
class SemBench : public Benchmark {
  sem_t sem;

 public:
  string get_description() const override {
    return "sem post + wait";
  }

  void start_up() override {
    int err = sem_init(&sem, 0, 0);
    CHECK(err != -1);
  }

  void run(int n) override {
    for (int i = 0; i < n; i++) {
      sem_post(&sem);
      sem_wait(&sem);
    }
  }

  void tear_down() override {
    sem_destroy(&sem);
  }
};
#endif

#if !TD_WINDOWS
class UtimeBench : public Benchmark {
 public:
  void start_up() override {
    FileFd::open("test", FileFd::Flags::Create | FileFd::Flags::Write).move_as_ok().close();
  }
  string get_description() const override {
    return "utime";
  }
  void run(int n) override {
    for (int i = 0; i < n; i++) {
      int err = utime("test", nullptr);
      CHECK(err >= 0);
      utimbuf buf;
      buf.modtime = 123;
      buf.actime = 321;
      err = utime("test", &buf);
      CHECK(err >= 0);
    }
  }
};
#endif

BENCH(Pwrite, "pwrite") {
  auto fd = FileFd::open("test", FileFd::Flags::Create | FileFd::Flags::Write).move_as_ok();
  for (int i = 0; i < n; i++) {
    fd.pwrite("a", 0).ok();
  }
  fd.close();
}

class CreateFileBench : public Benchmark {
  string get_description() const override {
    return "create_file";
  }
  void start_up() override {
    mkdir("A").ensure();
  }
  void run(int n) override {
    for (int i = 0; i < n; i++) {
      FileFd::open(PSLICE() << "A/" << i, FileFd::Flags::Write | FileFd::Flags::Create).move_as_ok().close();
    }
  }
  void tear_down() override {
    td::walk_path("A/", [&](CSlice path, auto type) {
      if (type == td::WalkPath::Type::ExitDir) {
        rmdir(path).ignore();
      } else if (type == td::WalkPath::Type::NotDir) {
        unlink(path).ignore();
      }
    }).ignore();
  }
};

class WalkPathBench : public Benchmark {
  string get_description() const override {
    return "walk_path";
  }
  void start_up_n(int n) override {
    mkdir("A").ensure();
    for (int i = 0; i < n; i++) {
      FileFd::open(PSLICE() << "A/" << i, FileFd::Flags::Write | FileFd::Flags::Create).move_as_ok().close();
    }
  }
  void run(int n) override {
    int cnt = 0;
    td::walk_path("A/", [&](CSlice path, auto type) {
      if (type == td::WalkPath::Type::EnterDir) {
        return;
      }
      stat(path).ok();
      cnt++;
    }).ignore();
  }
  void tear_down() override {
    td::walk_path("A/", [&](CSlice path, auto type) {
      if (type == td::WalkPath::Type::ExitDir) {
        rmdir(path).ignore();
      } else if (type == td::WalkPath::Type::NotDir) {
        unlink(path).ignore();
      }
    }).ignore();
  }
};

#if !TD_THREAD_UNSUPPORTED
template <int ThreadN = 2>
class AtomicReleaseIncBench : public Benchmark {
  string get_description() const override {
    return PSTRING() << "AtomicReleaseInc" << ThreadN;
  }

  static std::atomic<uint64> a_;
  void run(int n) override {
    std::vector<thread> threads;
    for (int i = 0; i < ThreadN; i++) {
      threads.emplace_back([&] {
        for (int i = 0; i < n / ThreadN; i++) {
          a_.fetch_add(1, std::memory_order_release);
        }
      });
    }
    for (auto &thread : threads) {
      thread.join();
    }
  }
};
template <int ThreadN>
std::atomic<uint64> AtomicReleaseIncBench<ThreadN>::a_;

template <int ThreadN = 2>
class AtomicReleaseCasIncBench : public Benchmark {
  string get_description() const override {
    return PSTRING() << "AtomicReleaseCasInc" << ThreadN;
  }

  static std::atomic<uint64> a_;
  void run(int n) override {
    std::vector<thread> threads;
    for (int i = 0; i < ThreadN; i++) {
      threads.emplace_back([&] {
        for (int i = 0; i < n / ThreadN; i++) {
          auto value = a_.load(std::memory_order_relaxed);
          while (!a_.compare_exchange_strong(value, value + 1, std::memory_order_release, std::memory_order_relaxed)) {
          }
        }
      });
    }
    for (auto &thread : threads) {
      thread.join();
    }
  }
};
template <int ThreadN>
std::atomic<uint64> AtomicReleaseCasIncBench<ThreadN>::a_;

template <int ThreadN = 2>
class RwMutexReadBench : public Benchmark {
  string get_description() const override {
    return PSTRING() << "RwMutexRead" << ThreadN;
  }
  RwMutex mutex_;
  void run(int n) override {
    std::vector<thread> threads;
    for (int i = 0; i < ThreadN; i++) {
      threads.emplace_back([&] {
        for (int i = 0; i < n / ThreadN; i++) {
          mutex_.lock_read().ensure();
        }
      });
    }
    for (auto &thread : threads) {
      thread.join();
    }
  }
};
template <int ThreadN = 2>
class RwMutexWriteBench : public Benchmark {
  string get_description() const override {
    return PSTRING() << "RwMutexWrite" << ThreadN;
  }
  RwMutex mutex_;
  void run(int n) override {
    std::vector<thread> threads;
    for (int i = 0; i < ThreadN; i++) {
      threads.emplace_back([&] {
        for (int i = 0; i < n / ThreadN; i++) {
          mutex_.lock_write().ensure();
        }
      });
    }
    for (auto &thread : threads) {
      thread.join();
    }
  }
};
#endif
}  // namespace td

int main() {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG));
#if !TD_THREAD_UNSUPPORTED
  td::bench(td::AtomicReleaseIncBench<1>());
  td::bench(td::AtomicReleaseIncBench<2>());
  td::bench(td::AtomicReleaseCasIncBench<1>());
  td::bench(td::AtomicReleaseCasIncBench<2>());
  td::bench(td::RwMutexWriteBench<1>());
  td::bench(td::RwMutexReadBench<1>());
  td::bench(td::RwMutexWriteBench<>());
  td::bench(td::RwMutexReadBench<>());
#endif
#if !TD_WINDOWS
  td::bench(td::UtimeBench());
#endif
  td::bench(td::WalkPathBench());
  td::bench(td::CreateFileBench());
  td::bench(td::PwriteBench());

  td::bench(td::CallBench());
#if !TD_THREAD_UNSUPPORTED
  td::bench(td::ThreadNewBench());
#endif
#if !TD_EVENTFD_UNSUPPORTED
  td::bench(td::EventFdBench());
#endif
  td::bench(td::NewObjBench());
  td::bench(td::NewIntBench());
#if !TD_WINDOWS
  td::bench(td::PipeBench());
#endif
#if TD_LINUX || TD_ANDROID || TD_TIZEN
  td::bench(td::SemBench());
#endif
}
