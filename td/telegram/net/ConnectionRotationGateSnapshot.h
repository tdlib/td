// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include <mutex>

namespace td {

struct ConnectionRotationGateSnapshot final {
  bool anti_churn_allows_rotation{true};
  bool destination_budget_allows_overlap{true};
};

class ConnectionRotationGateSnapshotHandle final {
 public:
  ConnectionRotationGateSnapshot get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
  }

  void set(ConnectionRotationGateSnapshot snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snapshot;
  }

 private:
  ConnectionRotationGateSnapshot snapshot_;
  mutable std::mutex mutex_;
};

}  // namespace td