//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"

#include <type_traits>
#include <utility>

namespace td {

template <class FunctionT>
class Action;

// move-only function, which can be called at most once
template <class R, class... Args>
class Action<R(Args...)> {
  class Impl {
   public:
    Impl() = default;
    Impl(const Impl &) = delete;
    Impl &operator=(const Impl &) = delete;
    Impl(Impl &&) = delete;
    Impl &operator=(Impl &&) = delete;
    virtual ~Impl() = default;

    virtual R call(Args &&...args) = 0;
  };

  template <class FunctionT>
  class FunctionImpl final : public Impl {
    FunctionT function_;

   public:
    template <class F>
    explicit FunctionImpl(F &&function) : function_(std::forward<F>(function)) {
    }

    R call(Args &&...args) final {
      return function_(std::forward<Args>(args)...);
    }
  };

  unique_ptr<Impl> impl_;

 public:
  Action() = default;

  template <class FunctionT, std::enable_if_t<!std::is_same<std::decay_t<FunctionT>, Action>::value, int> = 0>
  Action(FunctionT &&function)
      : impl_(make_unique<FunctionImpl<std::decay_t<FunctionT>>>(std::forward<FunctionT>(function))) {
  }

  Action(const Action &) = delete;
  Action &operator=(const Action &) = delete;
  Action(Action &&) = default;
  Action &operator=(Action &&) = default;
  ~Action() = default;

  explicit operator bool() const noexcept {
    return impl_ != nullptr;
  }

  R operator()(Args... args) {
    CHECK(impl_ != nullptr);
    auto impl = std::move(impl_);
    return impl->call(std::forward<Args>(args)...);
  }
};

using SimpleAction = Action<void()>;

inline void run_actions(vector<SimpleAction> &actions) {
  auto moved_actions = std::move(actions);
  actions.clear();
  for (auto &action : moved_actions) {
    action();
  }
}

}  // namespace td
