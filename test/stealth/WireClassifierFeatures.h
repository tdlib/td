// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "test/stealth/ReviewedFamilyLaneBaselines.h"

#include "td/utils/Status.h"

#include <array>

namespace td {
namespace mtproto {
namespace test {
namespace wire_classifier {

enum FeatureIndex : size_t {
  kWireLength = 0,
  kCipherCount = 1,
  kExtensionCount = 2,
  kAlpnCount = 3,
  kSupportedGroupsCount = 4,
  kKeyShareCount = 5,
  kEchPayloadLength = 6,
  kHasAlps = 7,
  kFeatureCount = 8,
};

struct SampleFeatures final {
  double wire_length{0};
  double cipher_count{0};
  double extension_count{0};
  double alpn_count{0};
  double supported_groups_count{0};
  double key_share_count{0};
  double ech_payload_length{0};
  double has_alps{0};
};

struct FeatureMask final {
  std::array<bool, kFeatureCount> enabled{};
};

SampleFeatures extract_generated_features(Slice wire);
SampleFeatures make_real_features_from_baseline_summary(const baselines::FamilyLaneBaseline &baseline, size_t index);
Result<vector<SampleFeatures>> load_real_features_for_family_lane(Slice family_id, Slice route_lane);
FeatureMask classifier_feature_mask_for_baseline(const baselines::FamilyLaneBaseline &baseline);
std::array<double, kFeatureCount> to_vector(const SampleFeatures &features, const FeatureMask &mask);

}  // namespace wire_classifier
}  // namespace test
}  // namespace mtproto
}  // namespace td