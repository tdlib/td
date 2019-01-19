//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/tests.h"
#include "td/utils/Random.h"

#include "td/telegram/SetWithPosition.h"

#include <set>

using namespace td;

template <class T, template <class> class Set = SetWithPosition>
class CheckedSetWithPosition {
 public:
  void add(int x) {
    s_.add(x);
    if (checked_.count(x) != 0) {
      return;
    }
    not_checked_.insert(x);
  }
  void remove(int x) {
    s_.remove(x);
    checked_.erase(x);
    not_checked_.erase(x);
  }
  bool has_next() {
    auto res = !not_checked_.empty();
    //LOG(ERROR) << res;
    ASSERT_EQ(res, s_.has_next());
    return res;
  }
  void reset_position() {
    s_.reset_position();
    not_checked_.insert(checked_.begin(), checked_.end());
    checked_ = {};
  }

  T next() {
    CHECK(has_next());
    auto next = s_.next();
    //LOG(ERROR) << next;
    ASSERT_TRUE(not_checked_.count(next) != 0);
    not_checked_.erase(next);
    checked_.insert(next);
    return next;
  }

  void merge(CheckedSetWithPosition &&other) {
    if (size() < other.size()) {
      std::swap(*this, other);
      std::swap(this->s_, other.s_);
    }
    for (auto x : other.checked_) {
      not_checked_.erase(x);
      checked_.insert(x);
    }
    for (auto x : other.not_checked_) {
      if (checked_.count(x) != 0) {
        continue;
      }
      not_checked_.insert(x);
    }
    s_.merge(std::move(other.s_));
  }
  size_t size() const {
    return checked_.size() + not_checked_.size();
  }

 private:
  std::set<T> checked_;
  std::set<T> not_checked_;
  Set<T> s_;
};

template <template <class> class RawSet>
void test_hands() {
  using Set = CheckedSetWithPosition<int, RawSet>;

  Set a;
  a.add(1);
  a.add(2);
  a.next();
  Set b;
  b.add(1);
  b.add(3);

  a.merge(std::move(b));
  while (a.has_next()) {
    a.next();
  }
}
template <template <class> class RawSet>
void test_stress() {
  Random::Xorshift128plus rnd(123);
  using Set = CheckedSetWithPosition<int, RawSet>;
  for (int t = 0; t < 100; t++) {
    std::vector<unique_ptr<Set>> sets(1000);
    for (auto &s : sets) {
      s = make_unique<Set>();
    }
    int n;
    auto merge = [&] {
      int a = rnd.fast(0, n - 2);
      int b = rnd.fast(a + 1, n - 1);
      std::swap(sets[b], sets[n - 1]);
      std::swap(sets[a], sets[n - 2]);
      a = n - 2;
      b = n - 1;
      if (rnd.fast(0, 1) == 0) {
        std::swap(sets[a], sets[b]);
      }
      sets[a]->merge(std::move(*sets[b]));
      sets.pop_back();
    };
    auto next = [&] {
      int i = rnd.fast(0, n - 1);
      if (sets[i]->has_next()) {
        sets[i]->next();
      }
    };
    auto add = [&] {
      int i = rnd.fast(0, n - 1);
      int x = rnd.fast(0, 10);
      sets[i]->add(x);
    };
    auto remove = [&] {
      int i = rnd.fast(0, n - 1);
      int x = rnd.fast(0, 10);
      sets[i]->remove(x);
    };
    auto reset_position = [&] {
      int i = rnd.fast(0, n - 1);
      sets[i]->reset_position();
    };
    struct Step {
      std::function<void()> func;
      td::uint32 weight;
    };
    std::vector<Step> steps{{merge, 1}, {next, 10}, {add, 10}, {remove, 10}, {reset_position, 5}};
    td::uint32 steps_sum = 0;
    for (auto &step : steps) {
      steps_sum += step.weight;
    }

    while (true) {
      n = static_cast<int>(sets.size());
      if (n == 1) {
        break;
      }
      auto w = rnd() % steps_sum;
      for (auto &step : steps) {
        if (w < step.weight) {
          step.func();
          break;
        }
        w -= step.weight;
      }
    }
  }
}
template <template <class> class RawSet>
void test_speed() {
  Random::Xorshift128plus rnd(123);
  using Set = CheckedSetWithPosition<int, RawSet>;
  std::vector<unique_ptr<Set>> sets(1 << 18);
  for (size_t i = 0; i < sets.size(); i++) {
    sets[i] = make_unique<Set>();
    sets[i]->add(int(i));
  }
  for (size_t d = 1; d < sets.size(); d *= 2) {
    for (size_t i = 0; i < sets.size(); i += 2 * d) {
      size_t j = i + d;
      CHECK(j < sets.size());
      sets[i]->merge(std::move(*sets[j]));
    }
  }
  LOG(ERROR) << sets[0]->size();
}

TEST(SetWithPosition, hands) {
  test_hands<FastSetWithPosition>();
  test_hands<OldSetWithPosition>();
  test_hands<SetWithPosition>();
}
TEST(SetWithPosition, stress) {
  test_stress<FastSetWithPosition>();
  test_stress<OldSetWithPosition>();
  test_stress<SetWithPosition>();
}
TEST(SetWithPosition, speed) {
  test_speed<FastSetWithPosition>();
  test_speed<SetWithPosition>();
}
