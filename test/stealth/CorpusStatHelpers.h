// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/ReviewedClientHelloFixtures.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/common.h"
#include "td/utils/crypto.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace td {
namespace mtproto {
namespace test {

template <class T>
struct FrequencyCounter final {
  void count(const T &value) {
    counts_[value]++;
    total_++;
  }

  uint32 observed(const T &value) const {
    auto it = counts_.find(value);
    return it == counts_.end() ? 0u : it->second;
  }

  size_t distinct_values() const {
    return counts_.size();
  }

  uint32 min_observed() const {
    if (counts_.empty()) {
      return 0;
    }
    uint32 result = std::numeric_limits<uint32>::max();
    for (const auto &it : counts_) {
      result = std::min(result, it.second);
    }
    return result;
  }

  uint32 max_observed() const {
    uint32 result = 0;
    for (const auto &it : counts_) {
      result = std::max(result, it.second);
    }
    return result;
  }

  double coverage_ratio(size_t expected_distinct) const {
    if (expected_distinct == 0) {
      return 1.0;
    }
    return static_cast<double>(distinct_values()) / static_cast<double>(expected_distinct);
  }

  uint32 total() const {
    return total_;
  }

  const std::unordered_map<T, uint32> &counts() const {
    return counts_;
  }

 private:
  std::unordered_map<T, uint32> counts_;
  uint32 total_{0};
};

template <class T>
std::unordered_set<T> make_unordered_set(const vector<T> &values) {
  return std::unordered_set<T>(values.begin(), values.end());
}

inline void check_uniform_distribution(const FrequencyCounter<uint16> &counter, size_t expected_n, size_t num_buckets,
                                       double min_fraction) {
  CHECK(num_buckets > 0);
  auto floor_per_bucket =
      static_cast<uint32>(static_cast<double>(expected_n) / static_cast<double>(num_buckets) * min_fraction);
  for (const auto &it : counter.counts()) {
    CHECK(it.second >= floor_per_bucket);
  }
}

inline string two_digit_decimal(size_t value) {
  char buffer[3] = {'0', '0', '\0'};
  std::snprintf(buffer, sizeof(buffer), "%02zu", value);
  return string(buffer);
}

inline string hex_u16(uint16 value) {
  char buffer[5] = {'0', '0', '0', '0', '\0'};
  std::snprintf(buffer, sizeof(buffer), "%04x", value);
  return string(buffer);
}

inline std::unordered_set<uint16> extension_set_non_grease_no_padding(const ParsedClientHello &hello) {
  std::unordered_set<uint16> result;
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type) && ext.type != 0x0015) {
      result.insert(ext.type);
    }
  }
  return result;
}

inline vector<uint16> non_grease_extension_sequence(const ParsedClientHello &hello) {
  vector<uint16> result;
  result.reserve(hello.extensions.size());
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type) && ext.type != 0x0015) {
      result.push_back(ext.type);
    }
  }
  return result;
}

inline FrequencyCounter<uint16> extension_type_counter(const ParsedClientHello &hello) {
  FrequencyCounter<uint16> counter;
  for (const auto &ext : hello.extensions) {
    counter.count(ext.type);
  }
  return counter;
}

inline string hex_encode_bytes(Slice data) {
  string result;
  result.reserve(data.size() * 2);
  for (size_t i = 0; i < data.size(); i++) {
    auto byte = static_cast<uint8>(data[i]);
    static const char kHex[] = "0123456789abcdef";
    result.push_back(kHex[byte >> 4]);
    result.push_back(kHex[byte & 0x0F]);
  }
  return result;
}

inline string compute_ja3_string(const ParsedClientHello &hello) {
  string result = "771,";

  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  bool first = true;
  for (auto cs : cipher_suites) {
    if (is_grease_value(cs)) {
      continue;
    }
    if (!first) {
      result += "-";
    }
    result += to_string(cs);
    first = false;
  }

  result += ",";
  first = true;
  for (const auto &ext : hello.extensions) {
    if (is_grease_value(ext.type)) {
      continue;
    }
    if (!first) {
      result += "-";
    }
    result += to_string(ext.type);
    first = false;
  }

  result += ",";
  if (auto *supported_groups = find_extension(hello, 0x000A)) {
    auto groups = parse_supported_groups(supported_groups->value).move_as_ok();
    first = true;
    for (auto group : groups) {
      if (is_grease_value(group)) {
        continue;
      }
      if (!first) {
        result += "-";
      }
      result += to_string(group);
      first = false;
    }
  }

  result += ",";
  if (auto *ec_point_formats = find_extension(hello, 0x000B)) {
    auto formats = parse_ec_point_formats_vector(ec_point_formats->value).move_as_ok();
    for (size_t i = 0; i < formats.size(); i++) {
      if (i != 0) {
        result += "-";
      }
      result += to_string(formats[i]);
    }
  }

  return result;
}

struct Ja4Segments final {
  string segment_a;
  string segment_b;
  string segment_c;
};

inline string compute_ja4_segment_a(const ParsedClientHello &hello) {
  string result = "t";

  auto *supported_versions = find_extension(hello, 0x002B);
  string version = "00";
  if (supported_versions != nullptr && supported_versions->value.size() >= 3) {
    auto versions_len = static_cast<uint8>(supported_versions->value[0]);
    for (size_t i = 1; i + 1 < supported_versions->value.size() && i < static_cast<size_t>(versions_len + 1); i += 2) {
      auto supported_version = static_cast<uint16>((static_cast<uint8>(supported_versions->value[i]) << 8) |
                                                   static_cast<uint8>(supported_versions->value[i + 1]));
      if (is_grease_value(supported_version)) {
        continue;
      }
      version = supported_version == 0x0304 ? "13" : (supported_version == 0x0303 ? "12" : "00");
      break;
    }
  }
  result += version;
  result += find_extension(hello, 0x0000) == nullptr ? "i" : "d";

  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  size_t cipher_count = 0;
  for (auto cs : cipher_suites) {
    if (!is_grease_value(cs)) {
      cipher_count++;
    }
  }
  result += two_digit_decimal(cipher_count);

  size_t extension_count = 0;
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type)) {
      extension_count++;
    }
  }
  result += two_digit_decimal(extension_count);

  auto *alpn = find_extension(hello, 0x0010);
  if (alpn != nullptr && alpn->value.size() >= 4) {
    auto protocol_len = static_cast<uint8>(alpn->value[2]);
    if (protocol_len > 0 && alpn->value.size() >= static_cast<size_t>(3 + protocol_len)) {
      result.push_back(alpn->value[3]);
      result.push_back(alpn->value[2 + protocol_len]);
    } else {
      result += "00";
    }
  } else {
    result += "00";
  }

  return result;
}

inline string compute_ja4_segment_b(const ParsedClientHello &hello) {
  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  vector<string> values;
  values.reserve(cipher_suites.size());
  for (auto cs : cipher_suites) {
    if (is_grease_value(cs)) {
      continue;
    }
    values.push_back(hex_u16(cs));
  }
  std::sort(values.begin(), values.end());
  string joined;
  for (size_t i = 0; i < values.size(); i++) {
    if (i != 0) {
      joined += ",";
    }
    joined += values[i];
  }
  string hash(32, '\0');
  sha256(joined, hash);
  return hex_encode_bytes(Slice(hash).substr(0, 6));
}

inline string compute_ja4_segment_c(const ParsedClientHello &hello) {
  vector<string> extensions;
  extensions.reserve(hello.extensions.size());
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type) && ext.type != 0x0000 && ext.type != 0x0010) {
      extensions.push_back(hex_u16(ext.type));
    }
  }
  std::sort(extensions.begin(), extensions.end());

  string joined;
  for (size_t i = 0; i < extensions.size(); i++) {
    if (i != 0) {
      joined += ",";
    }
    joined += extensions[i];
  }

  if (auto *sig_algorithms = find_extension(hello, 0x000D)) {
    joined += "_";
    if (sig_algorithms->value.size() >= 2) {
      auto sig_len = static_cast<uint16>((static_cast<uint8>(sig_algorithms->value[0]) << 8) |
                                         static_cast<uint8>(sig_algorithms->value[1]));
      for (size_t i = 2; i + 1 < sig_algorithms->value.size() && i < static_cast<size_t>(sig_len + 2); i += 2) {
        if (i != 2) {
          joined += ",";
        }
        auto sig = static_cast<uint16>((static_cast<uint8>(sig_algorithms->value[i]) << 8) |
                                       static_cast<uint8>(sig_algorithms->value[i + 1]));
        joined += hex_u16(sig);
      }
    }
  }

  string hash(32, '\0');
  sha256(joined, hash);
  return hex_encode_bytes(Slice(hash).substr(0, 6));
}

inline Ja4Segments compute_ja4_segments(const ParsedClientHello &hello) {
  return {compute_ja4_segment_a(hello), compute_ja4_segment_b(hello), compute_ja4_segment_c(hello)};
}

inline const auto kChrome133EchExtensionSet =
    make_unordered_set(fixtures::reviewed::chrome146_75_linux_desktopNonGreaseExtensionsWithoutPadding);
inline const auto kChrome133NoEchExtensionSet = [] {
  auto result = kChrome133EchExtensionSet;
  result.erase(fixtures::kEchExtensionType);
  return result;
}();
inline const auto kFirefox148ExtensionOrder =
    fixtures::reviewed::firefox148_linux_desktopNonGreaseExtensionsWithoutPadding;
inline const auto kSafariIosExtensionSet =
    make_unordered_set(fixtures::reviewed::safari26_3_1_ios26_3_1_aNonGreaseExtensionsWithoutPadding);

}  // namespace test
}  // namespace mtproto
}  // namespace td