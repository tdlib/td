// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/WireClassifierFeatures.h"

#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/filesystem.h"
#include "td/utils/JsonBuilder.h"

#include <filesystem>

namespace td {
namespace mtproto {
namespace test {
namespace wire_classifier {

namespace {

#ifndef TELEMT_TEST_REPO_ROOT
#define TELEMT_TEST_REPO_ROOT ""
#endif

constexpr uint16 kPaddingExtension = 0x0015u;
constexpr uint16 kAlpsDraftType = 0x4469u;
constexpr uint16 kAlpsChromeType = 0x44CDu;

string client_hello_fixture_root() {
  return string(TELEMT_TEST_REPO_ROOT) + "/test/analysis/fixtures/clienthello";
}

bool is_grease_group(uint16 value) {
  return (value & 0x0F0Fu) == 0x0A0Au && ((value >> 8) == (value & 0xFFu));
}

uint16 parse_hex_u16(Slice hex) {
  string s = hex.str();
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s = s.substr(2);
  }
  uint16 value = 0;
  for (char c : s) {
    int digit = -1;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = 10 + (c - 'a');
    } else if (c >= 'A' && c <= 'F') {
      digit = 10 + (c - 'A');
    } else {
      continue;
    }
    value = static_cast<uint16>((value << 4) | static_cast<uint16>(digit));
  }
  return value;
}

bool ends_with(Slice text, Slice suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool is_authoritative_source_kind(Slice source_kind) {
  const auto normalized = to_lower(source_kind.str());
  return normalized == "browser_capture" || normalized == "curl_cffi_capture" || normalized == "pcap";
}

bool contains_any_token(const string &text, std::initializer_list<const char *> tokens) {
  for (const auto *token : tokens) {
    if (text.find(token) != string::npos) {
      return true;
    }
  }
  return false;
}

string classify_family_id(Slice profile_id, Slice os_family) {
  const auto normalized = to_lower(profile_id.str());
  const auto normalized_os = to_lower(os_family.str());

  const bool is_firefox_gecko = contains_any_token(normalized, {"firefox", "librewolf", "ironfox", "firefoxzen"});
  const bool is_safari = contains_any_token(normalized, {"safari"});

  if (is_safari) {
    if (normalized_os == "ios") {
      return "apple_ios_tls";
    }
    if (normalized_os == "macos") {
      return "apple_macos_tls";
    }
    return "apple_tls";
  }

  if (is_firefox_gecko) {
    if (normalized_os == "ios") {
      return "apple_ios_tls";
    }
    if (normalized_os == "android") {
      return "firefox_android";
    }
    if (normalized_os == "macos") {
      return "firefox_macos";
    }
    if (normalized_os == "linux") {
      return "firefox_linux_desktop";
    }
    if (normalized_os == "windows") {
      return "firefox_windows";
    }
    return "firefox_other";
  }

  if (normalized_os == "ios") {
    return "ios_chromium";
  }
  if (normalized_os == "android") {
    return "android_chromium";
  }
  if (normalized_os == "macos") {
    return "chromium_macos";
  }
  if (normalized_os == "linux") {
    return "chromium_linux_desktop";
  }
  if (normalized_os == "windows") {
    return "chromium_windows";
  }
  return "chromium_other";
}

Result<vector<uint16>> parse_hex_string_array(JsonValue &value) {
  if (value.type() != JsonValue::Type::Array) {
    return Status::Error("Expected JSON array");
  }

  vector<uint16> out;
  for (auto &entry : value.get_array()) {
    if (entry.type() != JsonValue::Type::String) {
      return Status::Error("Expected hex-string array entry");
    }
    out.push_back(parse_hex_u16(entry.get_string()));
  }
  return out;
}

vector<uint16> strip_grease_and_optional_padding(vector<uint16> values, bool strip_padding) {
  vector<uint16> out;
  out.reserve(values.size());
  for (auto value : values) {
    if (is_grease_value(value)) {
      continue;
    }
    if (strip_padding && value == kPaddingExtension) {
      continue;
    }
    out.push_back(value);
  }
  return out;
}

Result<vector<uint16>> read_hex_array_field(JsonObject &object, Slice primary_field, Slice fallback_field,
                                            bool strip_padding) {
  auto primary = object.extract_optional_field(primary_field, JsonValue::Type::Array);
  if (primary.is_ok()) {
    auto value = primary.move_as_ok();
    TRY_RESULT(parsed, parse_hex_string_array(value));
    return parsed;
  }

  if (!fallback_field.empty()) {
    auto fallback = object.extract_optional_field(fallback_field, JsonValue::Type::Array);
    if (fallback.is_ok()) {
      auto value = fallback.move_as_ok();
      TRY_RESULT(parsed, parse_hex_string_array(value));
      return strip_grease_and_optional_padding(std::move(parsed), strip_padding);
    }
  }

  return vector<uint16>();
}

size_t count_key_share_entries(JsonObject &sample_obj) {
  auto key_shares = sample_obj.extract_optional_field("key_share_entries", JsonValue::Type::Array);
  if (key_shares.is_error()) {
    return 0;
  }

  size_t count = 0;
  for (auto &entry : key_shares.ok().get_array()) {
    if (entry.type() != JsonValue::Type::Object) {
      continue;
    }
    auto &entry_obj = entry.get_object();
    auto is_grease = entry_obj.get_optional_bool_field("is_grease_group", false);
    if (is_grease.is_error() || !is_grease.ok()) {
      count++;
    }
  }
  return count;
}

double read_ech_payload_length(JsonObject &sample_obj) {
  auto ech = sample_obj.extract_optional_field("ech", JsonValue::Type::Object);
  if (ech.is_error()) {
    return 0.0;
  }
  auto &ech_obj = ech.ok().get_object();
  auto payload_length = ech_obj.get_optional_long_field("payload_length", 0);
  if (payload_length.is_error() || payload_length.ok() < 0) {
    return 0.0;
  }
  return static_cast<double>(payload_length.ok());
}

Result<SampleFeatures> extract_real_features_from_fixture_sample(JsonValue &sample_value) {
  if (sample_value.type() != JsonValue::Type::Object) {
    return Status::Error("ClientHello fixture sample is not an object");
  }

  auto &sample_obj = sample_value.get_object();
  SampleFeatures features;

  auto record_length = sample_obj.get_optional_long_field("record_length", 0);
  if (record_length.is_ok() && record_length.ok() > 0) {
    features.wire_length = static_cast<double>(record_length.ok());
  } else {
    TRY_RESULT(handshake_length, sample_obj.get_optional_long_field("handshake_length", 0));
    features.wire_length = static_cast<double>(handshake_length);
  }

  TRY_RESULT(cipher_suites, read_hex_array_field(sample_obj, "non_grease_cipher_suites", "cipher_suites", false));
  features.cipher_count = static_cast<double>(cipher_suites.size());

  TRY_RESULT(extension_types,
             read_hex_array_field(sample_obj, "non_grease_extensions_without_padding", "extension_types", true));
  features.extension_count = static_cast<double>(extension_types.size());

  auto alpn_protocols = sample_obj.extract_optional_field("alpn_protocols", JsonValue::Type::Array);
  if (alpn_protocols.is_ok()) {
    features.alpn_count = static_cast<double>(alpn_protocols.ok().get_array().size());
  }

  TRY_RESULT(supported_groups,
             read_hex_array_field(sample_obj, "non_grease_supported_groups", "supported_groups", false));
  features.supported_groups_count = static_cast<double>(supported_groups.size());
  features.key_share_count = static_cast<double>(count_key_share_entries(sample_obj));
  features.ech_payload_length = read_ech_payload_length(sample_obj);

  features.has_alps = 0.0;
  for (auto extension_type : extension_types) {
    if (extension_type == kAlpsDraftType || extension_type == kAlpsChromeType) {
      features.has_alps = 1.0;
      break;
    }
  }

  return features;
}

Result<vector<SampleFeatures>> load_real_features_from_fixture_file(CSlice absolute_path, Slice family_id,
                                                                    Slice route_lane) {
  TRY_RESULT(buffer, read_file_str(absolute_path));
  TRY_RESULT(root, json_decode(MutableSlice(buffer)));
  if (root.type() != JsonValue::Type::Object) {
    return Status::Error("ClientHello fixture root is not an object");
  }

  auto &obj = root.get_object();
  TRY_RESULT(profile_id, obj.get_optional_string_field("profile_id"));
  TRY_RESULT(os_family, obj.get_optional_string_field("os_family"));
  TRY_RESULT(source_kind, obj.get_optional_string_field("source_kind"));
  TRY_RESULT(route_mode, obj.get_optional_string_field("route_mode"));

  if (!is_authoritative_source_kind(source_kind)) {
    return vector<SampleFeatures>();
  }
  if (route_mode != route_lane.str()) {
    return vector<SampleFeatures>();
  }
  if (classify_family_id(profile_id, os_family) != family_id.str()) {
    return vector<SampleFeatures>();
  }

  TRY_RESULT(samples_field, obj.extract_required_field("samples", JsonValue::Type::Array));
  vector<SampleFeatures> out;
  for (auto &sample_value : samples_field.get_array()) {
    TRY_RESULT(features, extract_real_features_from_fixture_sample(sample_value));
    out.push_back(features);
  }
  return out;
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

Result<vector<SampleFeatures>> load_real_features_for_family_lane(Slice family_id, Slice route_lane) {
  const auto *baseline = baselines::get_baseline(family_id, route_lane);
  if (baseline == nullptr) {
    return Status::Error("Unknown reviewed family-lane baseline");
  }
  if (baseline->authoritative_sample_count == 0u) {
    return Status::Error("Family-lane has no authoritative reviewed captures");
  }

  std::error_code iter_error;
  const std::filesystem::path root(client_hello_fixture_root());
  std::filesystem::recursive_directory_iterator iter(root, iter_error);
  if (iter_error) {
    return Status::Error(PSLICE() << "Failed to open ClientHello fixture root: " << root.native());
  }

  vector<SampleFeatures> out;
  out.reserve(baseline->authoritative_sample_count);

  for (const auto &entry : iter) {
    if (entry.is_directory()) {
      continue;
    }

    const auto filename = entry.path().filename().string();
    if (!ends_with(filename, ".clienthello.json")) {
      continue;
    }

    TRY_RESULT(file_samples, load_real_features_from_fixture_file(entry.path().string(), family_id, route_lane));
    for (auto &sample : file_samples) {
      out.push_back(std::move(sample));
    }
  }

  if (out.empty()) {
    return Status::Error("No reviewed authoritative fixture samples matched the requested family-lane");
  }
  if (out.size() != baseline->authoritative_sample_count) {
    return Status::Error(PSLICE() << "Reviewed fixture sample count mismatch for family-lane; expected "
                                  << baseline->authoritative_sample_count << ", got " << out.size());
  }
  return out;
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