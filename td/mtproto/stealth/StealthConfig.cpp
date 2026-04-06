//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/StealthConfig.h"

#include "td/utils/Random.h"
#include "td/utils/Time.h"

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
  TRY_STATUS(validate_range("ipt_params", static_cast<int32>(ipt_params.interactive_delay_min_us),
                            static_cast<int32>(ipt_params.interactive_delay_max_us), 0, 5 * 1000 * 1000));
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