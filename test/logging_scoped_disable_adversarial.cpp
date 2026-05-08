// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

namespace {

class VerbosityRestoreGuard final {
 public:
  explicit VerbosityRestoreGuard(int level) : level_(level) {
  }

  ~VerbosityRestoreGuard() {
    td::set_verbosity_level(level_);
  }

 private:
  int level_;
};

TEST(LoggingScopedDisableAdversarial, ConcurrentOverlappingScopesKeepLoggingDisabledUntilLastOwnerExits) {
  const auto original_level = td::get_verbosity_level();
  VerbosityRestoreGuard restore(original_level);

  td::set_verbosity_level(VERBOSITY_NAME(INFO));

  std::mutex mutex;
  std::condition_variable cv;
  bool worker_entered = false;
  bool worker_release = false;
  bool worker_finished = false;

  {
    td::ScopedDisableLog outer_disable;
    ASSERT_EQ(std::numeric_limits<int>::min(), td::get_verbosity_level());

    std::thread worker([&] {
      td::ScopedDisableLog inner_disable;
      {
        std::lock_guard<std::mutex> guard(mutex);
        worker_entered = true;
      }
      cv.notify_one();

      std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [&] { return worker_release; });
      lock.unlock();

      ASSERT_EQ(std::numeric_limits<int>::min(), td::get_verbosity_level());

      {
        std::lock_guard<std::mutex> guard(mutex);
        worker_finished = true;
      }
      cv.notify_one();
    });

    {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [&] { return worker_entered; });
    }

    ASSERT_EQ(std::numeric_limits<int>::min(), td::get_verbosity_level());

    {
      std::lock_guard<std::mutex> guard(mutex);
      worker_release = true;
    }
    cv.notify_one();

    {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [&] { return worker_finished; });
    }

    ASSERT_EQ(std::numeric_limits<int>::min(), td::get_verbosity_level());
    worker.join();
    ASSERT_EQ(std::numeric_limits<int>::min(), td::get_verbosity_level());
  }

  ASSERT_EQ(VERBOSITY_NAME(INFO), td::get_verbosity_level());
}

}  // namespace