//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include <atomic>
#include <memory>

namespace td {

namespace detail {
struct RawCancellationToken {
  std::atomic<bool> is_cancelled_{false};
};
}  // namespace detail

class CancellationToken {
 public:
  explicit operator bool() const {
    // empty CancellationToken is never cancelled
    if (!token_) {
      return false;
    }
    return token_->is_cancelled_.load(std::memory_order_acquire);
  }
  CancellationToken() = default;
  explicit CancellationToken(std::shared_ptr<detail::RawCancellationToken> token) : token_(std::move(token)) {
  }

 private:
  std::shared_ptr<detail::RawCancellationToken> token_;
};

class CancellationTokenSource {
 public:
  CancellationTokenSource() = default;
  CancellationTokenSource(CancellationTokenSource &&other) : token_(std::move(other.token_)) {
  }
  CancellationTokenSource &operator=(CancellationTokenSource &&other) {
    cancel();
    token_ = std::move(other.token_);
    return *this;
  }
  CancellationTokenSource(const CancellationTokenSource &other) = delete;
  CancellationTokenSource &operator=(const CancellationTokenSource &other) = delete;
  ~CancellationTokenSource() {
    cancel();
  }

  CancellationToken get_cancellation_token() {
    if (!token_) {
      token_ = std::make_shared<detail::RawCancellationToken>();
    }
    return CancellationToken(token_);
  }
  void cancel() {
    if (!token_) {
      return;
    }
    token_->is_cancelled_.store(true, std::memory_order_release);
    token_.reset();
  }

 private:
  std::shared_ptr<detail::RawCancellationToken> token_;
};

}  // namespace td
