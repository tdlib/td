// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
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