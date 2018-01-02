//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/List.h"
#include "td/utils/logging.h"
#include "td/utils/port/thread.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"

#include <atomic>

#define REGISTER_TESTS(x)                \
  void TD_CONCAT(register_tests_, x)() { \
  }
#define DESC_TESTS(x) void TD_CONCAT(register_tests_, x)()
#define LOAD_TESTS(x) TD_CONCAT(register_tests_, x)()

namespace td {

class Test : private ListNode {
 public:
  explicit Test(CSlice name) : name_(name) {
    get_tests_list()->put_back(this);
  }

  Test(const Test &) = delete;
  Test &operator=(const Test &) = delete;
  Test(Test &&) = delete;
  Test &operator=(Test &&) = delete;
  virtual ~Test() = default;

  static void add_substr_filter(std::string str) {
    if (str[0] != '+' && str[0] != '-') {
      str = "+" + str;
    }
    get_substr_filters()->push_back(std::move(str));
  }
  static void set_stress_flag(bool flag) {
    get_stress_flag() = flag;
  }
  static void run_all() {
    while (run_all_step()) {
    }
  }

  static bool run_all_step() {
    auto *state = get_state();
    if (state->it == nullptr) {
      state->end = get_tests_list();
      state->it = state->end->next;
    }

    while (state->it != state->end) {
      auto test = static_cast<td::Test *>(state->it);
      if (!state->is_running) {
        bool ok = true;
        for (const auto &filter : *get_substr_filters()) {
          bool is_match = test->name_.str().find(filter.substr(1)) != std::string::npos;
          if (is_match != (filter[0] == '+')) {
            ok = false;
            break;
          }
        }
        if (!ok) {
          state->it = state->it->next;
          continue;
        }
        LOG(ERROR) << "Run test " << tag("name", test->name_);
        state->start = Time::now();
        state->is_running = true;
      }

      if (test->step()) {
        break;
      }

      LOG(ERROR) << format::as_time(Time::now() - state->start);
      state->is_running = false;
      state->it = state->it->next;
    }

    auto ret = state->it != state->end;
    if (!ret) {
      *state = State();
    }
    return ret || get_stress_flag();
  }

 private:
  CSlice name_;
  struct State {
    ListNode *it = nullptr;
    bool is_running = false;
    double start;
    ListNode *end = nullptr;
  };
  static State *get_state() {
    static State state;
    return &state;
  }
  static std::vector<std::string> *get_substr_filters() {
    static std::vector<std::string> substr_filters_;
    return &substr_filters_;
  }

  static ListNode *get_tests_list() {
    static ListNode root;
    return &root;
  }
  static bool &get_ok_flag() {
    static bool is_ok = true;
    return is_ok;
  }
  static bool &get_stress_flag() {
    static bool stress_flag = false;
    return stress_flag;
  }
  virtual void run() {
    while (step()) {
    }
  }

  virtual bool step() {
    run();
    return false;
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

inline string rand_string(char from, char to, int len) {
  string res(len, 0);
  for (auto &c : res) {
    c = static_cast<char>(Random::fast(from, to));
  }
  return res;
}

inline std::vector<string> rand_split(string str) {
  std::vector<string> res;
  size_t pos = 0;
  while (pos < str.size()) {
    size_t len;
    if (Random::fast(0, 1) == 1) {
      len = Random::fast(1, 10);
    } else {
      len = Random::fast(100, 200);
    }
    res.push_back(str.substr(pos, len));
    pos += len;
  }
  return res;
}

template <class T1, class T2>
void assert_eq_impl(const T1 &expected, const T2 &got, const char *file, int line) {
  CHECK(expected == got) << tag("expected", expected) << tag("got", got) << " in " << file << " at line " << line;
}

template <class T>
void assert_true_impl(const T &got, const char *file, int line) {
  CHECK(got) << "Expected true in " << file << " at line " << line;
}

}  // namespace td

#define ASSERT_EQ(expected, got) ::td::assert_eq_impl((expected), (got), __FILE__, __LINE__)

#define ASSERT_TRUE(got) ::td::assert_true_impl((got), __FILE__, __LINE__)

#define ASSERT_STREQ(expected, got) \
  ::td::assert_eq_impl(::td::Slice((expected)), ::td::Slice((got)), __FILE__, __LINE__)

#define TEST_NAME(test_case_name, test_name) \
  TD_CONCAT(Test, TD_CONCAT(_, TD_CONCAT(test_case_name, TD_CONCAT(_, test_name))))

#define TEST(test_case_name, test_name) TEST_IMPL(TEST_NAME(test_case_name, test_name))

#define TEST_IMPL(test_name)                                                                     \
  class test_name : public ::td::Test {                                                          \
   public:                                                                                       \
    using Test::Test;                                                                            \
    void run() final;                                                                            \
  };                                                                                             \
  test_name TD_CONCAT(test_instance_, TD_CONCAT(test_name, __LINE__))(TD_DEFINE_STR(test_name)); \
  void test_name::run()
