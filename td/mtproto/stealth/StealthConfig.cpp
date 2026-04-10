// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthRecordSizeBaselines.h"
#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/utils/Random.h"
#include "td/utils/Time.h"

#include <cmath>

namespace td {
namespace mtproto {
namespace stealth {
namespace {

constexpr int32 kMaxTlsPayloadCap = 16384;
constexpr int32 kMinGreetingRecordSize = 80;
constexpr int32 kMaxGreetingRecordSize = 1500;

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

Status validate_positive_range(const char *name, int32 min_value, int32 max_value, int32 lower_bound,
                               int32 upper_bound) {
  if (min_value < lower_bound || max_value > upper_bound) {
    return Status::Error(std::string(name) + " is out of allowed bounds");
  }
  if (min_value <= 0 || max_value <= 0) {
    return Status::Error(std::string(name) + " must be positive");
  }
  if (min_value > max_value) {
    return Status::Error(std::string(name) + " min must not exceed max");
  }
  return Status::OK();
}

Status validate_size_t_range(const char *name, size_t value, size_t lower_bound, size_t upper_bound) {
  if (value < lower_bound || value > upper_bound) {
    return Status::Error(std::string(name) + " is out of allowed bounds");
  }
  return Status::OK();
}

Status validate_drs_phase_model(const char *name, const DrsPhaseModel &model, int32 min_payload_cap,
                                int32 max_payload_cap) {
  constexpr int32 kMaxSafeLocalJitter = (std::numeric_limits<int32>::max() - 1) / 2;

  if (model.bins.empty()) {
    return Status::Error(std::string(name) + " must not be empty");
  }
  if (model.max_repeat_run <= 0) {
    return Status::Error(std::string(name) + " max_repeat_run must be positive");
  }
  if (model.local_jitter < 0) {
    return Status::Error(std::string(name) + " local_jitter must be non-negative");
  }
  if (model.local_jitter > kMaxSafeLocalJitter) {
    return Status::Error(std::string(name) + " local_jitter exceeds supported range");
  }
  uint64 total_weight = 0;
  for (size_t i = 0; i < model.bins.size(); i++) {
    const auto &bin = model.bins[i];
    if (bin.weight == 0) {
      return Status::Error(std::string(name) + " bins must have positive weight");
    }
    if (bin.lo < min_payload_cap || bin.hi > max_payload_cap || bin.lo > bin.hi) {
      return Status::Error(std::string(name) + " bins must stay within payload bounds");
    }
    total_weight += bin.weight;
    if (total_weight > std::numeric_limits<uint32>::max()) {
      return Status::Error(std::string(name) + " total bin weight exceeds selection accumulator");
    }
  }
  return Status::OK();
}

int32 sample_phase_model_value(const DrsPhaseModel &model, int32 min_payload_cap, int32 max_payload_cap, IRng &rng) {
  uint32 total_weight = 0;
  for (const auto &bin : model.bins) {
    total_weight += bin.weight;
  }
  CHECK(total_weight != 0);

  uint32 selected = rng.bounded(total_weight);
  const RecordSizeBin *chosen = &model.bins.back();
  for (const auto &bin : model.bins) {
    if (selected < bin.weight) {
      chosen = &bin;
      break;
    }
    selected -= bin.weight;
  }

  auto width = static_cast<uint32>(chosen->hi - chosen->lo + 1);
  auto sampled = chosen->lo + static_cast<int32>(rng.bounded(width));
  if (model.local_jitter > 0) {
    auto jitter_width = static_cast<uint32>(model.local_jitter * 2 + 1);
    sampled += static_cast<int32>(rng.bounded(jitter_width)) - model.local_jitter;
  }
  sampled = std::max(chosen->lo, std::min(sampled, chosen->hi));
  sampled = std::max(min_payload_cap, std::min(sampled, max_payload_cap));
  return sampled;
}

Status validate_greeting_camouflage_policy(const GreetingCamouflagePolicy &policy) noexcept {
  if (policy.greeting_record_count > GreetingCamouflagePolicy::kMaxRecordModels) {
    return Status::Error("greeting_camouflage_policy.greeting_record_count exceeds available templates");
  }
  for (size_t i = 0; i < policy.greeting_record_count; i++) {
    auto name = std::string("greeting_camouflage_policy.record_models[") + std::to_string(i) + "]";
    TRY_STATUS(validate_drs_phase_model(name.c_str(), policy.record_models[i], kMinGreetingRecordSize,
                                        kMaxGreetingRecordSize));
  }
  return Status::OK();
}

Status validate_bidirectional_correlation_policy(const BidirectionalCorrelationPolicy &policy) noexcept {
  TRY_STATUS(validate_range("bidirectional_correlation_policy.small_response_threshold_bytes",
                            policy.small_response_threshold_bytes, policy.small_response_threshold_bytes, 1, 16384));
  TRY_STATUS(validate_range("bidirectional_correlation_policy.next_request_min_payload_cap",
                            policy.next_request_min_payload_cap, policy.next_request_min_payload_cap, 256, 16384));
  TRY_STATUS(validate_non_negative_finite("bidirectional_correlation_policy.post_response_delay_jitter_ms_min",
                                          policy.post_response_delay_jitter_ms_min));
  TRY_STATUS(validate_non_negative_finite("bidirectional_correlation_policy.post_response_delay_jitter_ms_max",
                                          policy.post_response_delay_jitter_ms_max));
  if (policy.post_response_delay_jitter_ms_min > policy.post_response_delay_jitter_ms_max) {
    return Status::Error("bidirectional_correlation_policy.post_response_delay_jitter_ms_min must not exceed max");
  }
  TRY_STATUS(validate_microsecond_delay_cap("bidirectional_correlation_policy.post_response_delay_jitter_ms_min",
                                            policy.post_response_delay_jitter_ms_min));
  TRY_STATUS(validate_microsecond_delay_cap("bidirectional_correlation_policy.post_response_delay_jitter_ms_max",
                                            policy.post_response_delay_jitter_ms_max));
  return Status::OK();
}

Status validate_chaff_policy(const ChaffPolicy &policy) noexcept {
  if (!policy.enabled) {
    return Status::OK();
  }
  TRY_STATUS(
      validate_range("chaff_policy.idle_threshold_ms", policy.idle_threshold_ms, policy.idle_threshold_ms, 1, 600000));
  TRY_STATUS(validate_non_negative_finite("chaff_policy.min_interval_ms", policy.min_interval_ms));
  if (policy.min_interval_ms == 0.0) {
    return Status::Error("chaff_policy.min_interval_ms must be positive");
  }
  TRY_STATUS(validate_microsecond_delay_cap("chaff_policy.min_interval_ms", policy.min_interval_ms));
  TRY_STATUS(validate_size_t_range("chaff_policy.max_bytes_per_minute", policy.max_bytes_per_minute, 1,
                                   static_cast<size_t>(1) << 20));
  TRY_STATUS(validate_drs_phase_model("chaff_policy.record_model", policy.record_model, 1, kMaxTlsPayloadCap));
  return Status::OK();
}

GreetingCamouflagePolicy make_default_greeting_camouflage_policy() {
  auto sanitize_record_bins = [](const auto &source_bins) {
    DrsPhaseModel model;
    model.max_repeat_run = 4;
    model.local_jitter = 0;
    for (const auto &bin : source_bins) {
      auto lo = std::max(bin.lo, kMinGreetingRecordSize);
      auto hi = std::min(bin.hi, kMaxGreetingRecordSize);
      if (lo > hi) {
        continue;
      }
      if (!model.bins.empty() && lo <= model.bins.back().hi) {
        lo = model.bins.back().hi + 1;
      }
      if (lo > hi) {
        continue;
      }
      model.bins.push_back({lo, hi, bin.weight});
    }
    CHECK(!model.bins.empty());
    return model;
  };

  GreetingCamouflagePolicy policy;
  policy.greeting_record_count = static_cast<uint8>(GreetingCamouflagePolicy::kMaxRecordModels);
  policy.record_models[0] = sanitize_record_bins(baselines::kGreetingRecord1);
  policy.record_models[1] = sanitize_record_bins(baselines::kGreetingRecord2);
  policy.record_models[2] = sanitize_record_bins(baselines::kGreetingRecord3);
  policy.record_models[3] = sanitize_record_bins(baselines::kGreetingRecord4);
  policy.record_models[4] = sanitize_record_bins(baselines::kGreetingRecord5);
  return policy;
}

ChaffPolicy make_default_chaff_policy() {
  ChaffPolicy policy;
  policy.enabled = false;
  policy.record_model.max_repeat_run = 4;
  policy.record_model.local_jitter = 0;
  for (const auto &bin : baselines::kIdleChaffBins) {
    policy.record_model.bins.push_back({bin.lo, bin.hi, bin.weight});
  }
  CHECK(!policy.record_model.bins.empty());
  return policy;
}

int32 payload_cap_from_record_size_limit(uint16 record_size_limit) {
  if (record_size_limit <= 1) {
    return 0;
  }
  return std::min<int32>(static_cast<int32>(record_size_limit) - 1, kMaxTlsPayloadCap);
}

void clamp_phase_model_to_max_payload_cap(DrsPhaseModel &model, int32 max_payload_cap) {
  for (auto &bin : model.bins) {
    bin.lo = std::min(bin.lo, max_payload_cap);
    bin.hi = std::min(bin.hi, max_payload_cap);
    if (bin.lo > bin.hi) {
      bin.lo = bin.hi;
    }
  }
}

void apply_profile_record_size_limit(StealthConfig &config) {
  auto record_size_limit = profile_spec(config.profile).record_size_limit;
  auto capped_payload_cap = payload_cap_from_record_size_limit(record_size_limit);
  if (capped_payload_cap == 0) {
    return;
  }

  config.drs_policy.max_payload_cap = std::min(config.drs_policy.max_payload_cap, capped_payload_cap);
  config.drs_policy.min_payload_cap = std::min(config.drs_policy.min_payload_cap, config.drs_policy.max_payload_cap);
  clamp_phase_model_to_max_payload_cap(config.drs_policy.slow_start, config.drs_policy.max_payload_cap);
  clamp_phase_model_to_max_payload_cap(config.drs_policy.congestion_open, config.drs_policy.max_payload_cap);
  clamp_phase_model_to_max_payload_cap(config.drs_policy.steady_state, config.drs_policy.max_payload_cap);

  config.record_size_policy.slow_start_max =
      std::min(config.record_size_policy.slow_start_max, config.drs_policy.max_payload_cap);
  config.record_size_policy.slow_start_min =
      std::min(config.record_size_policy.slow_start_min, config.record_size_policy.slow_start_max);
}

}  // namespace

unique_ptr<IRng> make_connection_rng() {
  return td::make_unique<SecureRng>();
}

unique_ptr<IClock> make_clock() {
  return td::make_unique<SystemClock>();
}

Status validate_ipt_params(const IptParams &params) noexcept {
  TRY_STATUS(validate_finite("ipt_params.burst_mu_ms", params.burst_mu_ms));
  TRY_STATUS(validate_non_negative_finite("ipt_params.burst_sigma", params.burst_sigma));
  TRY_STATUS(validate_non_negative_finite("ipt_params.burst_max_ms", params.burst_max_ms));
  TRY_STATUS(validate_non_negative_finite("ipt_params.idle_alpha", params.idle_alpha));
  TRY_STATUS(validate_non_negative_finite("ipt_params.idle_scale_ms", params.idle_scale_ms));
  TRY_STATUS(validate_non_negative_finite("ipt_params.idle_max_ms", params.idle_max_ms));
  TRY_STATUS(validate_probability("ipt_params.p_burst_stay", params.p_burst_stay));
  TRY_STATUS(validate_probability("ipt_params.p_idle_to_burst", params.p_idle_to_burst));
  if (params.burst_max_ms == 0.0) {
    return Status::Error("ipt_params.burst_max_ms must be positive");
  }
  TRY_STATUS(validate_microsecond_delay_cap("ipt_params.burst_max_ms", params.burst_max_ms));
  if (params.idle_alpha == 0.0) {
    return Status::Error("ipt_params.idle_alpha must be positive");
  }
  if (params.idle_scale_ms == 0.0) {
    return Status::Error("ipt_params.idle_scale_ms must be positive");
  }
  if (params.idle_scale_ms >= params.idle_max_ms) {
    return Status::Error("ipt_params.idle_scale_ms must be below idle_max_ms");
  }
  TRY_STATUS(validate_microsecond_delay_cap("ipt_params.idle_max_ms", params.idle_max_ms));
  return Status::OK();
}

Status validate_drs_policy(const DrsPolicy &policy) noexcept {
  TRY_STATUS(
      validate_positive_range("drs_policy.payload_cap", policy.min_payload_cap, policy.max_payload_cap, 256, 16384));
  if (policy.slow_start_records <= 0) {
    return Status::Error("drs_policy.slow_start_records must be positive");
  }
  if (policy.congestion_bytes <= 0) {
    return Status::Error("drs_policy.congestion_bytes must be positive");
  }
  TRY_STATUS(validate_positive_range("drs_policy.idle_reset_ms", policy.idle_reset_ms_min, policy.idle_reset_ms_max, 1,
                                     60000));
  TRY_STATUS(validate_drs_phase_model("drs_policy.slow_start", policy.slow_start, policy.min_payload_cap,
                                      policy.max_payload_cap));
  TRY_STATUS(validate_drs_phase_model("drs_policy.congestion_open", policy.congestion_open, policy.min_payload_cap,
                                      policy.max_payload_cap));
  TRY_STATUS(validate_drs_phase_model("drs_policy.steady_state", policy.steady_state, policy.min_payload_cap,
                                      policy.max_payload_cap));
  return Status::OK();
}

Status validate_record_padding_policy(const RecordPaddingPolicy &policy) noexcept {
  TRY_STATUS(validate_range("record_padding_policy.small_record_threshold", policy.small_record_threshold,
                            policy.small_record_threshold, 200, 16384));
  TRY_STATUS(validate_probability("record_padding_policy.small_record_max_fraction", policy.small_record_max_fraction));
  TRY_STATUS(validate_range("record_padding_policy.small_record_window_size", policy.small_record_window_size,
                            policy.small_record_window_size, 1, 4096));
  TRY_STATUS(validate_range("record_padding_policy.target_tolerance", policy.target_tolerance, policy.target_tolerance,
                            0, 1024));
  return Status::OK();
}

StealthConfig StealthConfig::default_config(IRng &rng) {
  (void)rng;
  auto runtime_params = get_runtime_stealth_params_snapshot();
  StealthConfig config;
  config.profile = BrowserProfile::Chrome133;
  config.padding_policy = no_padding_policy();
  config.crypto_padding_policy.enabled = true;
  config.chaff_policy = make_default_chaff_policy();
  config.ipt_params = runtime_params.ipt_params;
  config.drs_policy = runtime_params.drs_policy;
  config.bulk_threshold_bytes = runtime_params.bulk_threshold_bytes;
  return config;
}

StealthConfig StealthConfig::from_secret(const ProxySecret &secret, IRng &rng) {
  auto unix_time = static_cast<int32>(Time::now_cached());
  return from_secret(secret, rng, unix_time, default_runtime_platform_hints());
}

StealthConfig StealthConfig::from_secret(const ProxySecret &secret, IRng &rng, int32 unix_time,
                                         const RuntimePlatformHints &platform) {
  auto config = default_config(rng);
  if (secret.emulate_tls()) {
#if !TD_DARWIN
    config.profile = pick_runtime_profile(secret.get_domain(), unix_time, platform);
    apply_profile_record_size_limit(config);
#endif
    config.padding_policy.enabled = false;
    config.greeting_camouflage_policy = make_default_greeting_camouflage_policy();
    config.chaff_policy.enabled = true;
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
  TRY_STATUS(validate_ipt_params(ipt_params));
  TRY_STATUS(validate_drs_policy(drs_policy));
  TRY_STATUS(validate_record_padding_policy(record_padding_policy));
  TRY_STATUS(validate_greeting_camouflage_policy(greeting_camouflage_policy));
  TRY_STATUS(validate_bidirectional_correlation_policy(bidirectional_correlation_policy));
  TRY_STATUS(validate_chaff_policy(chaff_policy));
  TRY_STATUS(validate_size_t_range("bulk_threshold_bytes", bulk_threshold_bytes, 512, static_cast<size_t>(1) << 20));
  TRY_STATUS(validate_positive_range("crypto_padding_policy", crypto_padding_policy.min_padding_bytes,
                                     crypto_padding_policy.max_padding_bytes, CryptoPaddingPolicy::kMinPaddingBytes,
                                     CryptoPaddingPolicy::kMaxPaddingBytes));
  TRY_STATUS(validate_range("record_size_policy", record_size_policy.slow_start_min, record_size_policy.slow_start_max,
                            256, 16384));
  return Status::OK();
}

int32 StealthConfig::sample_initial_record_size(IRng &rng) const {
  CHECK(validate().is_ok());
  auto width = static_cast<uint32>(record_size_policy.slow_start_max - record_size_policy.slow_start_min + 1);
  return record_size_policy.slow_start_min + static_cast<int32>(rng.bounded(width));
}

int32 StealthConfig::sample_greeting_record_size(uint8 record_index, IRng &rng) const {
  CHECK(validate().is_ok());
  CHECK(record_index < greeting_camouflage_policy.greeting_record_count);
  return sample_phase_model_value(greeting_camouflage_policy.record_models[record_index], kMinGreetingRecordSize,
                                  kMaxGreetingRecordSize, rng);
}

int32 StealthConfig::sample_chaff_record_size(IRng &rng) const {
  CHECK(validate().is_ok());
  CHECK(chaff_policy.enabled);
  return sample_phase_model_value(chaff_policy.record_model, 1, kMaxTlsPayloadCap, rng);
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