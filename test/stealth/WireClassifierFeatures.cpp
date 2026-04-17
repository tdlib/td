// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/WireClassifierFeatures.h"

#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

namespace td {
namespace mtproto {
namespace test {
namespace wire_classifier {

namespace {

bool is_grease_group(uint16 value) {
  return (value & 0x0F0Fu) == 0x0A0Au && ((value >> 8) == (value & 0xFFu));
}

size_t count_alpn_entries(const ParsedClientHello &parsed) {
  size_t alpn_entries = 0;
  if (auto *alpn = find_extension(parsed, 0x0010)) {
    if (alpn->value.size() >= 2) {
      size_t alpn_len = (static_cast<uint8>(alpn->value[0]) << 8) | static_cast<uint8>(alpn->value[1]);
      size_t pos = 2;
      while (pos < 2 + alpn_len && pos < alpn->value.size()) {
        auto proto_len = static_cast<uint8>(alpn->value[pos]);
        pos += 1 + proto_len;
        alpn_entries++;
      }
    }
  }
  return alpn_entries;
}

}  // namespace

SampleFeatures extract_generated_features(Slice wire) {
  auto parsed_result = parse_tls_client_hello(wire);
  CHECK(parsed_result.is_ok());
  const auto &parsed = parsed_result.ok();

  SampleFeatures features;
  features.wire_length = static_cast<double>(wire.size());

  size_t cipher_count = 0;
  auto ciphers = parse_cipher_suite_vector(parsed.cipher_suites).move_as_ok();
  for (auto cs : ciphers) {
    if (!is_grease_value(cs)) {
      cipher_count++;
    }
  }
  features.cipher_count = static_cast<double>(cipher_count);

  size_t ext_count = 0;
  for (const auto &ext : parsed.extensions) {
    if (!is_grease_value(ext.type) && ext.type != 0x0015u) {
      ext_count++;
    }
  }
  features.extension_count = static_cast<double>(ext_count);

  features.alpn_count = static_cast<double>(count_alpn_entries(parsed));

  size_t supported_groups_count = 0;
  for (auto group : parsed.supported_groups) {
    if (!is_grease_group(group)) {
      supported_groups_count++;
    }
  }
  features.supported_groups_count = static_cast<double>(supported_groups_count);

  features.key_share_count = features.supported_groups_count > 0 ? 2.0 : 0.0;
  features.ech_payload_length = static_cast<double>(parsed.ech_payload_length);

  bool has_alps = false;
  for (const auto &ext : parsed.extensions) {
    if (ext.type == 0x4469u || ext.type == 0x44CDu) {
      has_alps = true;
      break;
    }
  }
  features.has_alps = has_alps ? 1.0 : 0.0;
  return features;
}

SampleFeatures make_real_features_from_baseline_summary(const baselines::FamilyLaneBaseline &baseline, size_t index) {
  SampleFeatures features;
  const auto &catalog = baseline.set_catalog;

  CHECK(!catalog.observed_wire_lengths.empty());
  CHECK(!catalog.observed_extension_order_templates.empty());

  const auto wire_length = catalog.observed_wire_lengths[index % catalog.observed_wire_lengths.size()];
  const auto &extension_order =
      catalog.observed_extension_order_templates[index % catalog.observed_extension_order_templates.size()];

  features.wire_length = static_cast<double>(wire_length);
  features.cipher_count = static_cast<double>(baseline.invariants.non_grease_cipher_suites_ordered.size());
  features.extension_count = static_cast<double>(extension_order.size());
  features.alpn_count = static_cast<double>(baseline.invariants.alpn_protocols.size());
  features.supported_groups_count = static_cast<double>(baseline.invariants.non_grease_supported_groups.size());
  features.key_share_count = features.supported_groups_count > 0 ? 2.0 : 0.0;

  if (!catalog.observed_ech_payload_lengths.empty()) {
    features.ech_payload_length =
        static_cast<double>(catalog.observed_ech_payload_lengths[index % catalog.observed_ech_payload_lengths.size()]);
  }

  features.has_alps = catalog.observed_alps_types.empty() ? 0.0 : 1.0;
  return features;
}

FeatureMask classifier_feature_mask_for_baseline(const baselines::FamilyLaneBaseline &baseline) {
  FeatureMask mask;
  mask.enabled.fill(true);
  if (baseline.invariants.alpn_protocols.empty()) {
    bool has_alpn_extension = false;
    for (const auto &templ : baseline.set_catalog.observed_extension_order_templates) {
      if (std::find(templ.begin(), templ.end(), static_cast<uint16>(0x0010u)) != templ.end()) {
        has_alpn_extension = true;
        break;
      }
    }
    if (has_alpn_extension) {
      mask.enabled[kAlpnCount] = false;
    }
  }
  return mask;
}

std::array<double, kFeatureCount> to_vector(const SampleFeatures &features, const FeatureMask &mask) {
  std::array<double, kFeatureCount> values = {features.wire_length,
                                              features.cipher_count,
                                              features.extension_count,
                                              features.alpn_count,
                                              features.supported_groups_count,
                                              features.key_share_count,
                                              features.ech_payload_length,
                                              features.has_alps};
  for (size_t i = 0; i < values.size(); i++) {
    if (!mask.enabled[i]) {
      values[i] = 0.0;
    }
  }
  return values;
}

}  // namespace wire_classifier
}  // namespace test
}  // namespace mtproto
}  // namespace td