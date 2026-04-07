//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/stealth/Interfaces.h"

namespace td {
namespace mtproto {
namespace stealth {

struct IptParams final {
  double burst_mu_ms{3.5};
  double burst_sigma{0.8};
  double burst_max_ms{200.0};

  double idle_alpha{1.5};
  double idle_scale_ms{500.0};
  double idle_max_ms{3000.0};

  double p_burst_stay{0.95};
  double p_idle_to_burst{0.30};
};

class IptController final {
 public:
  explicit IptController(const IptParams &params, IRng &rng);

  uint64 next_delay_us(bool has_pending_data, TrafficHint hint);

 private:
  enum class State : uint8 { Idle = 0, Burst = 1 };

  static bool is_bypass_hint(TrafficHint hint);
  static TrafficHint normalize_hint(TrafficHint hint);

  State transition(bool has_pending_data);
  double sample_uniform01();
  double sample_normal();
  double sample_lognormal(double mu, double sigma);
  double sample_truncated_pareto(double u, double alpha, double scale, double max_value) const;

  IptParams params_;
  IRng &rng_;
  State state_{State::Idle};
  bool has_spare_normal_{false};
  double spare_normal_{0.0};
};

}  // namespace stealth
}  // namespace mtproto
}  // namespace td