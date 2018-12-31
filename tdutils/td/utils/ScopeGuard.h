//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2017
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"

#include <type_traits>
#include <utility>

namespace td {

namespace detail {
template <class FunctionT>
class ScopeGuard {
 public:
  explicit ScopeGuard(const FunctionT &func) : func_(func) {
  }
  explicit ScopeGuard(FunctionT &&func) : func_(std::move(func)) {
  }
  ScopeGuard(const ScopeGuard &other) = delete;
  ScopeGuard &operator=(const ScopeGuard &other) = delete;
  ScopeGuard(ScopeGuard &&other) : dismissed_(other.dismissed_), func_(std::move(other.func_)) {
    other.dismissed_ = true;
  }
  ScopeGuard &operator=(ScopeGuard &&other) = delete;

  void dismiss() {
    dismissed_ = true;
  }

  ~ScopeGuard() {
    if (!dismissed_) {
      func_();
    }
  }

 private:
  bool dismissed_ = false;
  FunctionT func_;
};
}  // namespace detail

enum class ScopeExit {};

template <class FunctionT>
auto operator+(ScopeExit, FunctionT &&func) {
  return detail::ScopeGuard<std::decay_t<FunctionT>>(std::forward<FunctionT>(func));
}

}  // namespace td

#define SCOPE_EXIT auto TD_CONCAT(SCOPE_EXIT_VAR_, __LINE__) = ::td::ScopeExit() + [&]()
