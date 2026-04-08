// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/Interfaces.h"

#include "td/utils/buffer.h"

#include <optional>
#include <vector>

namespace td {
namespace mtproto {
namespace stealth {

struct ShaperPendingWrite final {
  BufferWriter message;
  bool quick_ack{false};
  double send_at{0.0};
  TrafficHint hint{TrafficHint::Unknown};
};

class ShaperRingBuffer final {
 public:
  static constexpr size_t kDefaultCapacity = 32;

  explicit ShaperRingBuffer(size_t capacity = kDefaultCapacity);

  bool try_enqueue(ShaperPendingWrite &&item);

  template <class CallbackT>
  void drain_ready(double now, CallbackT &&callback) {
    while (size_ != 0) {
      auto &front = items_[head_];
      CHECK(front.has_value());
      if (front->send_at > now) {
        break;
      }

      if (!callback(front.value())) {
        break;
      }

      front.reset();
      head_ = (head_ + 1) % items_.size();
      size_--;
    }
  }

  size_t size() const noexcept {
    return size_;
  }

  bool empty() const noexcept {
    return size_ == 0;
  }

  double earliest_deadline() const noexcept;

 private:
  std::vector<std::optional<ShaperPendingWrite>> items_;
  size_t head_{0};
  size_t size_{0};
};

}  // namespace stealth
}  // namespace mtproto
}  // namespace td