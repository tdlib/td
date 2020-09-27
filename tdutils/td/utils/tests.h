//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Context.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/port/thread.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <atomic>
#include <functional>
#include <utility>

#define REGISTER_TESTS(x)                \
  void TD_CONCAT(register_tests_, x)() { \
  }
#define DESC_TESTS(x) void TD_CONCAT(register_tests_, x)()
#define LOAD_TESTS(x) TD_CONCAT(register_tests_, x)()

namespace td {

class RandomSteps {
 public:
  struct Step {
    std::function<void()> func;
    uint32 weight;
  };

  explicit RandomSteps(vector<Step> steps) : steps_(std::move(steps)) {
    for (const auto &step : steps_) {
      steps_sum_ += step.weight;
    }
  }

  template <class Random>
  void step(Random &rnd) const {
    auto w = rnd() % steps_sum_;
    for (const auto &step : steps_) {
      if (w < step.weight) {
        step.func();
        break;
      }
      w -= step.weight;
    }
  }

 private:
  vector<Step> steps_;
  int32 steps_sum_ = 0;
};

class RegressionTester {
 public:
  virtual ~RegressionTester() = default;
  static void destroy(CSlice db_path);
  static unique_ptr<RegressionTester> create(string db_path, string db_cache_dir = "");

  virtual Status verify_test(Slice name, Slice result) = 0;
  virtual void save_db() = 0;
};

class Test {
 public:
  virtual ~Test() = default;
  virtual void run() {
    while (step()) {
    }
  }
  virtual bool step() {
    run();
    return false;
  }
  Test() = default;
  Test(const Test &) = delete;
  Test &operator=(const Test &) = delete;
  Test(Test &&) = delete;
  Test &operator=(Test &&) = delete;
};

class TestContext : public Context<TestContext> {
 public:
  virtual ~TestContext() = default;
  virtual Slice name() = 0;
  virtual Status verify(Slice data) = 0;
};

class TestsRunner : public TestContext {
 public:
  static TestsRunner &get_default();

  void add_test(string name, std::function<unique_ptr<Test>()> test);
  void add_substr_filter(string str);
  void set_stress_flag(bool flag);
  void run_all();
  bool run_all_step();
  void set_regression_tester(unique_ptr<RegressionTester> regression_tester);

 private:
  struct State {
    size_t it{0};
    bool is_running = false;
    double start{0};
    double start_unadjusted{0};
    size_t end{0};
  };
  bool stress_flag_{false};
  vector<string> substr_filters_;
  struct TestInfo {
    std::function<unique_ptr<Test>()> creator;
    unique_ptr<Test> test;
  };
  vector<std::pair<string, TestInfo>> tests_;
  State state_;
  unique_ptr<RegressionTester> regression_tester_;

  Slice name() override;
  Status verify(Slice data) override;
};

template <class T>
class RegisterTest {
 public:
  explicit RegisterTest(string name, TestsRunner &runner = TestsRunner::get_default()) {
    runner.add_test(name, [] { return make_unique<T>(); });
  }
};

class Stage {
 public:
  void wait(uint64 need) {
    value_.fetch_add(1, std::memory_order_release);
    while (value_.load(std::memory_order_acquire) < need) {
      td::this_thread::yield();
    }
  };

 private:
  std::atomic<uint64> value_{0};
};

string rand_string(int from, int to, size_t len);

vector<string> rand_split(Slice str);

template <class T1, class T2>
void assert_eq_impl(const T1 &expected, const T2 &got, const char *file, int line) {
  LOG_CHECK(expected == got) << tag("expected", expected) << tag("got", got) << " in " << file << " at line " << line;
}

template <class T>
void assert_true_impl(const T &got, const char *file, int line) {
  LOG_CHECK(got) << "Expected true in " << file << " at line " << line;
}

}  // namespace td

#define ASSERT_EQ(expected, got) ::td::assert_eq_impl((expected), (got), __FILE__, __LINE__)

#define ASSERT_TRUE(got) ::td::assert_true_impl((got), __FILE__, __LINE__)

#define ASSERT_STREQ(expected, got) \
  ::td::assert_eq_impl(::td::Slice((expected)), ::td::Slice((got)), __FILE__, __LINE__)

#define REGRESSION_VERIFY(data) ::td::TestContext::get()->verify(data).ensure()

#define TEST_NAME(test_case_name, test_name) \
  TD_CONCAT(Test, TD_CONCAT(_, TD_CONCAT(test_case_name, TD_CONCAT(_, test_name))))

#define TEST(test_case_name, test_name) TEST_IMPL(TEST_NAME(test_case_name, test_name))

#define TEST_IMPL(test_name)                                                                                         \
  class test_name : public ::td::Test {                                                                              \
   public:                                                                                                           \
    using Test::Test;                                                                                                \
    void run() final;                                                                                                \
  };                                                                                                                 \
  ::td::RegisterTest<test_name> TD_CONCAT(test_instance_, TD_CONCAT(test_name, __LINE__))(TD_DEFINE_STR(test_name)); \
  void test_name::run()
