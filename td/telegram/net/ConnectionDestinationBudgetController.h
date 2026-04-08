// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include <deque>

namespace td {

class ConnectionDestinationBudgetController final {
 public:
  struct DestinationKey final {
    int32 dc_id{0};
    int32 proxy_id{0};
    bool allow_media_only{false};
    bool is_media{false};

    bool operator==(const DestinationKey &other) const {
      return dc_id == other.dc_id && proxy_id == other.proxy_id && allow_media_only == other.allow_media_only &&
             is_media == other.is_media;
    }
  };

  double get_wakeup_at(double now, const DestinationKey &destination,
                       const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy);
  void on_connect_started(double now, const DestinationKey &destination,
                          const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy);

 private:
  struct Attempt final {
    double started_at{0.0};
    DestinationKey destination;
  };

  struct DestinationState final {
    double last_started_at{0.0};
    DestinationKey destination;
  };

  void prune_old_attempts(double now);
  void prune_destination_state(double now, const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy);
  static double destination_share_window_seconds();

  std::deque<Attempt> recent_attempts_;
  std::deque<DestinationState> destination_state_;
};

}  // namespace td