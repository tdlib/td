// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/IptController.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/Status.h"

#include <array>

namespace td {
namespace mtproto {
namespace stealth {

struct RecordSizeBin final {
  int32 lo{0};
  int32 hi{0};
  uint16 weight{0};
};

struct DrsPhaseModel final {
  vector<RecordSizeBin> bins;
  int32 max_repeat_run{4};
  int32 local_jitter{24};
};

struct StealthConfig;

struct DrsPolicy final {
  DrsPhaseModel slow_start;
  DrsPhaseModel congestion_open;
  DrsPhaseModel steady_state;
  int32 slow_start_records{4};
  int32 congestion_bytes{32768};
  int32 idle_reset_ms_min{250};
  int32 idle_reset_ms_max{1200};
  int32 min_payload_cap{900};
  int32 max_payload_cap{16384};
};

struct RecordSizePolicy final {
  int32 slow_start_min{1200};
  int32 slow_start_max{1460};
};

struct CryptoPaddingPolicy final {
  static constexpr uint16 kMinPaddingBytes = 12;
  static constexpr uint16 kDefaultMaxPaddingBytes = 480;
  static constexpr uint16 kMaxPaddingBytes = 4096;

  bool enabled{true};
  uint16 min_padding_bytes{kMinPaddingBytes};
  uint16 max_padding_bytes{kDefaultMaxPaddingBytes};
};

struct RecordPaddingPolicy final {
  int32 small_record_threshold{200};
  double small_record_max_fraction{0.05};
  int32 small_record_window_size{200};
  int32 target_tolerance{32};
};

struct GreetingCamouflagePolicy final {
  static constexpr size_t kMaxRecordModels = 5;

  uint8 greeting_record_count{0};
  std::array<DrsPhaseModel, kMaxRecordModels> record_models;
};

struct BidirectionalCorrelationPolicy final {
  bool enabled{true};
  int32 small_response_threshold_bytes{192};
  int32 next_request_min_payload_cap{1200};
  double post_response_delay_jitter_ms_min{4.0};
  double post_response_delay_jitter_ms_max{24.0};
};

struct ChaffPolicy final {
  bool enabled{false};
  int32 idle_threshold_ms{15000};
  double min_interval_ms{5000.0};
  size_t max_bytes_per_minute{4096};
  DrsPhaseModel record_model;
};

Status validate_ipt_params(const IptParams &params) noexcept;
Status validate_drs_policy(const DrsPolicy &policy) noexcept;

struct StealthConfig final {
  static constexpr size_t kMaxRingCapacity = 4096;

  BrowserProfile profile{BrowserProfile::Chrome133};
  PaddingPolicy padding_policy;
  CryptoPaddingPolicy crypto_padding_policy;
  RecordPaddingPolicy record_padding_policy;
  GreetingCamouflagePolicy greeting_camouflage_policy;
  BidirectionalCorrelationPolicy bidirectional_correlation_policy;
  ChaffPolicy chaff_policy;
  IptParams ipt_params;
  DrsPolicy drs_policy;
  RecordSizePolicy record_size_policy;
  size_t bulk_threshold_bytes{8192};
  size_t ring_capacity{32};
  size_t high_watermark{24};
  size_t low_watermark{8};

  static StealthConfig from_secret(const ProxySecret &secret, IRng &rng);
  static StealthConfig from_secret(const ProxySecret &secret, IRng &rng, int32 unix_time,
                                   const RuntimePlatformHints &platform);
  static StealthConfig default_config(IRng &rng);

  Status validate() const;
  int32 sample_initial_record_size(IRng &rng) const;
  int32 sample_greeting_record_size(uint8 record_index, IRng &rng) const;
  int32 sample_chaff_record_size(IRng &rng) const;
};

using StealthConfigFactory = Result<StealthConfig> (*)(const ProxySecret &secret, IRng &rng);

Result<StealthConfig> make_transport_stealth_config(const ProxySecret &secret, IRng &rng);
StealthConfigFactory set_stealth_config_factory_for_tests(StealthConfigFactory factory);

}  // namespace stealth
}  // namespace mtproto
}  // namespace td