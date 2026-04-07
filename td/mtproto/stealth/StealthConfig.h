//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
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

struct StealthConfig;

struct DrsPolicy final {
  int32 record_size_min{1200};
  int32 record_size_max{1460};
};

struct RecordSizePolicy final {
  int32 slow_start_min{1200};
  int32 slow_start_max{1460};
};

struct StealthConfig final {
  static constexpr size_t kMaxRingCapacity = 4096;

  BrowserProfile profile{BrowserProfile::Chrome133};
  PaddingPolicy padding_policy;
  IptParams ipt_params;
  DrsPolicy drs_policy;
  RecordSizePolicy record_size_policy;
  size_t ring_capacity{32};
  size_t high_watermark{24};
  size_t low_watermark{8};

  static StealthConfig from_secret(const ProxySecret &secret, IRng &rng);
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