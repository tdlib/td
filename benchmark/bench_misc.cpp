//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
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
#include "td/utils/SliceBuilder.h"

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

class F {
  td::uint32 &sum;

 public:
  explicit F(td::uint32 &sum) : sum(sum) {
  }

  template <class T>
  void operator()(const T &x) const {
    sum += static_cast<td::uint32>(x.get_id());
  }
};

BENCH(Call, "TL Call") {
  td::tl_object_ptr<td::telegram_api::Function> x = td::make_tl_object<td::telegram_api::account_getWallPapers>(0);
  td::uint32 res = 0;
  F f(res);
  for (int i = 0; i < n; i++) {
    downcast_call(*x, f);
  }
  td::do_not_optimize_away(res);
}

#if !TD_EVENTFD_UNSUPPORTED
BENCH(EventFd, "EventFd") {
  td::EventFd fd;
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
  td::do_not_optimize_away(res);
}

BENCH(NewObj, "new struct, then delete") {
  struct A {
    td::int32 a = 0;
    td::int32 b = 0;
    td::int32 c = 0;
    td::int32 d = 0;
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
  td::do_not_optimize_away(res);
}

#if !TD_THREAD_UNSUPPORTED
BENCH(ThreadNew, "new struct, then delete in several threads") {
  NewObjBench a, b;
  td::thread ta([&] { a.run(n / 2); });
  td::thread tb([&] { b.run(n - n / 2); });
  ta.join();
  tb.join();
}
#endif
/*
// Too hard for clang (?)
BENCH(Time, "Clocks::monotonic") {
  double res = 0;
  for (int i = 0; i < n; i++) {
    res += td::Clocks::monotonic();
  }
  td::do_not_optimize_away(res);
}
*/
#if !TD_WINDOWS
class PipeBench final : public td::Benchmark {
 public:
  int p[2];

  td::string get_description() const final {
    return "pipe write + read int32";
  }

  void start_up() final {
    int res = pipe(p);
    CHECK(res == 0);
  }

  void run(int n) final {
    int res = 0;
    for (int i = 0; i < n; i++) {
      int val = 1;
      auto write_len = write(p[1], &val, sizeof(val));
      CHECK(write_len == sizeof(val));
      auto read_len = read(p[0], &val, sizeof(val));
      CHECK(read_len == sizeof(val));
      res += val;
    }
    td::do_not_optimize_away(res);
  }

  void tear_down() final {
    close(p[0]);
    close(p[1]);
  }
};
#endif

#if TD_LINUX || TD_ANDROID || TD_TIZEN
class SemBench final : public td::Benchmark {
  sem_t sem;

 public:
  td::string get_description() const final {
    return "sem post + wait";
  }

  void start_up() final {
    int err = sem_init(&sem, 0, 0);
    CHECK(err != -1);
  }

  void run(int n) final {
    for (int i = 0; i < n; i++) {
      sem_post(&sem);
      sem_wait(&sem);
    }
  }

  void tear_down() final {
    sem_destroy(&sem);
  }
};
#endif

#if !TD_WINDOWS
class UtimeBench final : public td::Benchmark {
 public:
  void start_up() final {
    td::FileFd::open("test", td::FileFd::Create | td::FileFd::Write).move_as_ok().close();
  }
  td::string get_description() const final {
    return "utime";
  }
  void run(int n) final {
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
  auto fd = td::FileFd::open("test", td::FileFd::Create | td::FileFd::Write).move_as_ok();
  for (int i = 0; i < n; i++) {
    fd.pwrite("a", 0).ok();
  }
  fd.close();
}

class CreateFileBench final : public td::Benchmark {
  td::string get_description() const final {
    return "create_file";
  }
  void start_up() final {
    td::mkdir("A").ensure();
  }
  void run(int n) final {
    for (int i = 0; i < n; i++) {
      td::FileFd::open(PSLICE() << "A/" << i, td::FileFd::Write | td::FileFd::Create).move_as_ok().close();
    }
  }
  void tear_down() final {
    td::walk_path("A/", [&](td::CSlice path, auto type) {
      if (type == td::WalkPath::Type::ExitDir) {
        td::rmdir(path).ignore();
      } else if (type == td::WalkPath::Type::NotDir) {
        td::unlink(path).ignore();
      }
    }).ignore();
  }
};

class WalkPathBench final : public td::Benchmark {
  td::string get_description() const final {
    return "walk_path";
  }
  void start_up_n(int n) final {
    td::mkdir("A").ensure();
    for (int i = 0; i < n; i++) {
      td::FileFd::open(PSLICE() << "A/" << i, td::FileFd::Write | td::FileFd::Create).move_as_ok().close();
    }
  }
  void run(int n) final {
    int cnt = 0;
    td::walk_path("A/", [&](td::CSlice path, auto type) {
      if (type == td::WalkPath::Type::EnterDir) {
        return;
      }
      td::stat(path).ok();
      cnt++;
    }).ignore();
  }
  void tear_down() final {
    td::walk_path("A/", [&](td::CSlice path, auto type) {
      if (type == td::WalkPath::Type::ExitDir) {
        td::rmdir(path).ignore();
      } else if (type == td::WalkPath::Type::NotDir) {
        td::unlink(path).ignore();
      }
    }).ignore();
  }
};

#if !TD_THREAD_UNSUPPORTED
template <int ThreadN = 2>
class AtomicReleaseIncBench final : public td::Benchmark {
  td::string get_description() const final {
    return PSTRING() << "AtomicReleaseInc" << ThreadN;
  }

  static std::atomic<td::uint64> a_;
  void run(int n) final {
    td::vector<td::thread> threads;
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
std::atomic<td::uint64> AtomicReleaseIncBench<ThreadN>::a_;

template <int ThreadN = 2>
class AtomicReleaseCasIncBench final : public td::Benchmark {
  td::string get_description() const final {
    return PSTRING() << "AtomicReleaseCasInc" << ThreadN;
  }

  static std::atomic<td::uint64> a_;
  void run(int n) final {
    td::vector<td::thread> threads;
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
std::atomic<td::uint64> AtomicReleaseCasIncBench<ThreadN>::a_;

template <int ThreadN = 2>
class RwMutexReadBench final : public td::Benchmark {
  td::string get_description() const final {
    return PSTRING() << "RwMutexRead" << ThreadN;
  }
  td::RwMutex mutex_;
  void run(int n) final {
    td::vector<td::thread> threads;
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
class RwMutexWriteBench final : public td::Benchmark {
  td::string get_description() const final {
    return PSTRING() << "RwMutexWrite" << ThreadN;
  }
  td::RwMutex mutex_;
  void run(int n) final {
    td::vector<td::thread> threads;
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

int main() {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG));
#if !TD_THREAD_UNSUPPORTED
  td::bench(AtomicReleaseIncBench<1>());
  td::bench(AtomicReleaseIncBench<2>());
  td::bench(AtomicReleaseCasIncBench<1>());
  td::bench(AtomicReleaseCasIncBench<2>());
  td::bench(RwMutexWriteBench<1>());
  td::bench(RwMutexReadBench<1>());
  td::bench(RwMutexWriteBench<>());
  td::bench(RwMutexReadBench<>());
#endif
#if !TD_WINDOWS
  td::bench(UtimeBench());
#endif
  td::bench(WalkPathBench());
  td::bench(CreateFileBench());
  td::bench(PwriteBench());

  td::bench(CallBench());
#if !TD_THREAD_UNSUPPORTED
  td::bench(ThreadNewBench());
#endif
#if !TD_EVENTFD_UNSUPPORTED
  td::bench(EventFdBench());
#endif
  td::bench(NewObjBench());
  td::bench(NewIntBench());
#if !TD_WINDOWS
  td::bench(PipeBench());
#endif
#if TD_LINUX || TD_ANDROID || TD_TIZEN
  td::bench(SemBench());
#endif
}
