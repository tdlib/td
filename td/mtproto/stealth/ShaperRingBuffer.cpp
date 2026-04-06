//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/ShaperRingBuffer.h"

namespace td {
namespace mtproto {
namespace stealth {

ShaperRingBuffer::ShaperRingBuffer(size_t capacity) : items_(capacity == 0 ? 1 : capacity) {
}

bool ShaperRingBuffer::try_enqueue(ShaperPendingWrite &&item) {
  if (size_ == items_.size()) {
    return false;
  }

  auto tail = (head_ + size_) % items_.size();
  items_[tail].emplace(std::move(item));
  size_++;
  return true;
}

double ShaperRingBuffer::earliest_deadline() const noexcept {
  if (size_ == 0) {
    return 0.0;
  }
  const auto &front = items_[head_];
  CHECK(front.has_value());
  return front->send_at;
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td