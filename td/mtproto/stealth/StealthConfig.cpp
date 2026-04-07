//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/StealthConfig.h"

#include "td/utils/Random.h"
#include "td/utils/Time.h"

#include <cmath>

namespace td {
namespace mtproto {
namespace stealth {
namespace {

StealthConfigFactory transport_stealth_config_factory = nullptr;

class SecureRng final : public IRng {
 public:
  void fill_secure_bytes(MutableSlice dest) final {
    Random::secure_bytes(dest);
  }

  uint32 secure_uint32() final {
    return Random::secure_uint32();
  }

  uint32 bounded(uint32 n) final {
    CHECK(n != 0);
    return Random::secure_uint32() % n;
  }
};

class SystemClock final : public IClock {
 public:
  double now() const final {
    return Time::now_cached();
  }
};

Status validate_range(const char *name, int32 min_value, int32 max_value, int32 lower_bound, int32 upper_bound) {
  if (min_value < lower_bound || max_value > upper_bound) {
    return Status::Error(std::string(name) + " is out of allowed bounds");
  }
  if (min_value > max_value) {
    return Status::Error(std::string(name) + " min must not exceed max");
  }
  return Status::OK();
}

Status validate_non_negative_finite(const char *name, double value) {
  if (!std::isfinite(value) || value < 0.0) {
    return Status::Error(std::string(name) + " must be finite and non-negative");
  }
  return Status::OK();
}

Status validate_finite(const char *name, double value) {
  if (!std::isfinite(value)) {
    return Status::Error(std::string(name) + " must be finite");
  }
  return Status::OK();
}

Status validate_probability(const char *name, double value) {
  if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
    return Status::Error(std::string(name) + " must be within [0, 1]");
  }
  return Status::OK();
}

Status validate_microsecond_delay_cap(const char *name, double value_ms) {
  constexpr double kMaxRepresentableDelayMs = static_cast<double>(std::numeric_limits<uint64>::max()) / 1000.0;
  if (value_ms > kMaxRepresentableDelayMs) {
    return Status::Error(std::string(name) + " must fit into uint64 microseconds");
  }
  return Status::OK();
}

}  // namespace

unique_ptr<IRng> make_connection_rng() {
  return td::make_unique<SecureRng>();
}

unique_ptr<IClock> make_clock() {
  return td::make_unique<SystemClock>();
}

StealthConfig StealthConfig::default_config(IRng &rng) {
  (void)rng;
  StealthConfig config;
  config.profile = BrowserProfile::Chrome133;
  config.padding_policy = no_padding_policy();
  return config;
}

StealthConfig StealthConfig::from_secret(const ProxySecret &secret, IRng &rng) {
  auto config = default_config(rng);
  if (secret.emulate_tls()) {
    config.padding_policy.enabled = false;
  }
  return config;
}

Status StealthConfig::validate() const {
  if (ring_capacity == 0) {
    return Status::Error("ring_capacity must be positive");
  }
  if (ring_capacity > kMaxRingCapacity) {
    return Status::Error("ring_capacity exceeds fail-closed maximum");
  }
  if (high_watermark >= ring_capacity) {
    return Status::Error("high_watermark must be below ring_capacity");
  }
  if (low_watermark > high_watermark) {
    return Status::Error("low_watermark must not exceed high_watermark");
  }
  TRY_STATUS(validate_finite("ipt_params.burst_mu_ms", ipt_params.burst_mu_ms));
  TRY_STATUS(validate_non_negative_finite("ipt_params.burst_sigma", ipt_params.burst_sigma));
  TRY_STATUS(validate_non_negative_finite("ipt_params.burst_max_ms", ipt_params.burst_max_ms));
  TRY_STATUS(validate_non_negative_finite("ipt_params.idle_alpha", ipt_params.idle_alpha));
  TRY_STATUS(validate_non_negative_finite("ipt_params.idle_scale_ms", ipt_params.idle_scale_ms));
  TRY_STATUS(validate_non_negative_finite("ipt_params.idle_max_ms", ipt_params.idle_max_ms));
  TRY_STATUS(validate_probability("ipt_params.p_burst_stay", ipt_params.p_burst_stay));
  TRY_STATUS(validate_probability("ipt_params.p_idle_to_burst", ipt_params.p_idle_to_burst));
  if (ipt_params.burst_max_ms == 0.0) {
    return Status::Error("ipt_params.burst_max_ms must be positive");
  }
  TRY_STATUS(validate_microsecond_delay_cap("ipt_params.burst_max_ms", ipt_params.burst_max_ms));
  if (ipt_params.idle_alpha == 0.0) {
    return Status::Error("ipt_params.idle_alpha must be positive");
  }
  if (ipt_params.idle_scale_ms == 0.0) {
    return Status::Error("ipt_params.idle_scale_ms must be positive");
  }
  if (ipt_params.idle_scale_ms >= ipt_params.idle_max_ms) {
    return Status::Error("ipt_params.idle_scale_ms must be below idle_max_ms");
  }
  TRY_STATUS(validate_microsecond_delay_cap("ipt_params.idle_max_ms", ipt_params.idle_max_ms));
  TRY_STATUS(validate_range("drs_policy", drs_policy.record_size_min, drs_policy.record_size_max, 256, 16384));
  TRY_STATUS(validate_range("record_size_policy", record_size_policy.slow_start_min, record_size_policy.slow_start_max,
                            256, 16384));
  return Status::OK();
}

int32 StealthConfig::sample_initial_record_size(IRng &rng) const {
  CHECK(validate().is_ok());
  auto width = static_cast<uint32>(record_size_policy.slow_start_max - record_size_policy.slow_start_min + 1);
  return record_size_policy.slow_start_min + static_cast<int32>(rng.bounded(width));
}

Result<StealthConfig> make_transport_stealth_config(const ProxySecret &secret, IRng &rng) {
  if (transport_stealth_config_factory != nullptr) {
    return transport_stealth_config_factory(secret, rng);
  }
  auto config = StealthConfig::from_secret(secret, rng);
  TRY_STATUS(config.validate());
  return config;
}

StealthConfigFactory set_stealth_config_factory_for_tests(StealthConfigFactory factory) {
  auto previous = transport_stealth_config_factory;
  transport_stealth_config_factory = factory;
  return previous;
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td