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

Status validate_ipt_params(const IptParams &params) noexcept;
Status validate_drs_policy(const DrsPolicy &policy) noexcept;

struct StealthConfig final {
  static constexpr size_t kMaxRingCapacity = 4096;

  BrowserProfile profile{BrowserProfile::Chrome133};
  PaddingPolicy padding_policy;
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
};

using StealthConfigFactory = Result<StealthConfig> (*)(const ProxySecret &secret, IRng &rng);

Result<StealthConfig> make_transport_stealth_config(const ProxySecret &secret, IRng &rng);
StealthConfigFactory set_stealth_config_factory_for_tests(StealthConfigFactory factory);

}  // namespace stealth
}  // namespace mtproto
}  // namespace td