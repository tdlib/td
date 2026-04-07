//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/IptController.h"

#include <algorithm>
#include <cmath>

namespace td {
namespace mtproto {
namespace stealth {

namespace {

constexpr double kUint32Denominator = 4294967296.0;
constexpr double kMinUnitInterval = 1e-9;
constexpr double kMaxUnitInterval = 1.0 - 1e-9;
constexpr double kTwoPi = 6.28318530717958647692;

uint64 to_delay_us(double delay_ms) {
  if (!(delay_ms > 0.0)) {
    return 0;
  }

  auto delay_us = static_cast<uint64>(delay_ms * 1000.0);
  return delay_us == 0 ? 1 : delay_us;
}

}  // namespace

IptController::IptController(const IptParams &params, IRng &rng) : params_(params), rng_(rng) {
}

uint64 IptController::next_delay_us(bool has_pending_data, TrafficHint hint) {
  hint = normalize_hint(hint);
  if (is_bypass_hint(hint)) {
    return 0;
  }

  state_ = transition(has_pending_data);
  double delay_ms = 0.0;
  if (state_ == State::Burst) {
    delay_ms = std::min(sample_lognormal(params_.burst_mu_ms, params_.burst_sigma), params_.burst_max_ms);
  } else {
    if (!has_pending_data) {
      return 0;
    }
    delay_ms =
        sample_truncated_pareto(sample_uniform01(), params_.idle_alpha, params_.idle_scale_ms, params_.idle_max_ms);
  }

  return to_delay_us(delay_ms);
}

bool IptController::is_bypass_hint(TrafficHint hint) {
  return hint == TrafficHint::Keepalive || hint == TrafficHint::BulkData || hint == TrafficHint::AuthHandshake;
}

TrafficHint IptController::normalize_hint(TrafficHint hint) {
  return hint == TrafficHint::Unknown ? TrafficHint::Interactive : hint;
}

IptController::State IptController::transition(bool has_pending_data) {
  if (!has_pending_data) {
    state_ = State::Idle;
    return state_;
  }

  const auto u = sample_uniform01();
  if (state_ == State::Burst) {
    state_ = u < params_.p_burst_stay ? State::Burst : State::Idle;
  } else {
    state_ = u < params_.p_idle_to_burst ? State::Burst : State::Idle;
  }
  return state_;
}

double IptController::sample_uniform01() {
  auto u = static_cast<double>(rng_.secure_uint32()) / kUint32Denominator;
  return std::clamp(u, kMinUnitInterval, kMaxUnitInterval);
}

double IptController::sample_normal() {
  if (has_spare_normal_) {
    has_spare_normal_ = false;
    return spare_normal_;
  }

  const auto u1 = sample_uniform01();
  const auto u2 = sample_uniform01();
  const auto radius = std::sqrt(-2.0 * std::log(u1));
  const auto theta = kTwoPi * u2;
  spare_normal_ = radius * std::sin(theta);
  has_spare_normal_ = true;
  return radius * std::cos(theta);
}

double IptController::sample_lognormal(double mu, double sigma) {
  if (sigma == 0.0) {
    return std::exp(mu);
  }
  return std::exp(mu + sigma * sample_normal());
}

double IptController::sample_truncated_pareto(double u, double alpha, double scale, double max_value) const {
  if (scale >= max_value) {
    return max_value;
  }

  const auto support_factor = 1.0 - std::pow(scale / max_value, alpha);
  const auto scaled_u = std::clamp(u * support_factor, 0.0, kMaxUnitInterval);
  auto value = scale / std::pow(1.0 - scaled_u, 1.0 / alpha);
  if (value >= max_value) {
    value = std::nextafter(max_value, 0.0);
  }
  return value;
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td