// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Nightly-scale cross-family pairwise distance. Asserts that every
// (X, Y) pair of distinct browser families stays above a conservative
// composite-distance floor across 10k seeds. Gated on
// TD_NIGHTLY_CORPUS; PR-scope returns in 0ms.

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_set>

namespace {

using namespace td;
using namespace td::mtproto;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

constexpr int32 kUnixTime = 1712345678;
constexpr uint64 kNightlyIterations = kFullIterations * 10;

// Reviewed 97.5th-percentile composite-distance floor. Families that
// ship the same stack are trivially distinguishable; a 0.30 composite
// drop below indicates a structural regression (two profiles leaking
// towards each other), which must be surfaced, not masked.
constexpr double kCompositeDistanceFloor = 0.30;

string build_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
}

EchMode ech_mode_for_profile(BrowserProfile profile) {
  const auto &spec = profile_spec(profile);
  return spec.allows_ech ? EchMode::Rfc9180Outer : EchMode::Disabled;
}

// --- Jaccard distance over non-GREASE extension sets -------------
double jaccard_distance(const std::unordered_set<uint16> &a, const std::unordered_set<uint16> &b) {
  if (a.empty() && b.empty()) {
    return 0.0;
  }
  size_t intersect = 0;
  for (auto v : a) {
    if (b.count(v) != 0) {
      intersect++;
    }
  }
  size_t union_size = a.size() + b.size() - intersect;
  if (union_size == 0) {
    return 0.0;
  }
  double similarity = static_cast<double>(intersect) / static_cast<double>(union_size);
  return 1.0 - similarity;
}

// --- Kendall-tau distance over cipher-suite order ----------------
// Simplified to a normalized count of adjacent-pair inversions
// between two equal-length sequences. When lengths differ (different
// non-GREASE cipher counts), we pin the distance to 1.0.
double kendall_tau_distance(const vector<uint16> &a, const vector<uint16> &b) {
  if (a.size() != b.size() || a.empty()) {
    return a == b ? 0.0 : 1.0;
  }
  // Build a positional index for b: b[i] -> position.
  // If any element in `a` is missing from `b`, fall back to 1.0.
  size_t n = a.size();
  vector<size_t> pos_in_b;
  pos_in_b.reserve(n);
  for (auto v : a) {
    size_t found = n;
    for (size_t i = 0; i < b.size(); i++) {
      if (b[i] == v) {
        found = i;
        break;
      }
    }
    if (found == n) {
      return 1.0;
    }
    pos_in_b.push_back(found);
  }
  size_t inversions = 0;
  size_t max_pairs = n * (n - 1) / 2;
  for (size_t i = 0; i < n; i++) {
    for (size_t j = i + 1; j < n; j++) {
      if (pos_in_b[i] > pos_in_b[j]) {
        inversions++;
      }
    }
  }
  if (max_pairs == 0) {
    return 0.0;
  }
  return static_cast<double>(inversions) / static_cast<double>(max_pairs);
}

// --- Hamming distance on a 16-bit feature vector ----------------
// Bit layout:
//  0: ECH present
//  1: PSK / pre_shared_key present
//  2: ALPS present (0x44CD or 0x4469)
//  3: record_size_limit present (0x001C)
//  4: compress_certificate present (0x001B)
//  5: delegated_credentials present (0x0022)
//  6: PQ hybrid group present in key_share
//  7: padding present
//  8: session_ticket (0x0023) present
//  9: ALPN h2 advertised (indirect: ALPN present)
// 10: extended_master_secret (0x0017)
// 11: status_request (0x0005)
// 12: psk_key_exchange_modes (0x002D)
// 13: supported_versions (0x002B)
// 14: server_name (0x0000)
// 15: signature_algorithms (0x000D)
uint16 feature_bits(const ParsedClientHello &hello) {
  uint16 bits = 0;
  auto set = [&bits](int shift, bool v) {
    if (v) {
      bits = static_cast<uint16>(bits | (1u << shift));
    }
  };
  set(0, find_extension(hello, 0xFE0D) != nullptr);
  set(1, find_extension(hello, 0x0029) != nullptr);
  set(2, find_extension(hello, 0x44CD) != nullptr || find_extension(hello, 0x4469) != nullptr);
  set(3, find_extension(hello, 0x001C) != nullptr);
  set(4, find_extension(hello, 0x001B) != nullptr);
  set(5, find_extension(hello, 0x0022) != nullptr);
  bool saw_pq = false;
  for (auto group : hello.key_share_groups) {
    if (group == fixtures::kPqHybridGroup || group == fixtures::kPqHybridDraftGroup) {
      saw_pq = true;
      break;
    }
  }
  set(6, saw_pq);
  set(7, find_extension(hello, 0x0015) != nullptr);
  set(8, find_extension(hello, 0x0023) != nullptr);
  set(9, find_extension(hello, 0x0010) != nullptr);
  set(10, find_extension(hello, 0x0017) != nullptr);
  set(11, find_extension(hello, 0x0005) != nullptr);
  set(12, find_extension(hello, 0x002D) != nullptr);
  set(13, find_extension(hello, 0x002B) != nullptr);
  set(14, find_extension(hello, 0x0000) != nullptr);
  set(15, find_extension(hello, 0x000D) != nullptr);
  return bits;
}

double hamming_distance_u16(uint16 x, uint16 y) {
  uint16 xor_bits = static_cast<uint16>(x ^ y);
  int count = 0;
  for (int i = 0; i < 16; i++) {
    if ((xor_bits >> i) & 1u) {
      count++;
    }
  }
  return static_cast<double>(count) / 16.0;
}

// --- Normalized wire-length delta ----------------------------------
double length_delta_normalized(size_t a, size_t b) {
  size_t max_len = std::max(a, b);
  if (max_len == 0) {
    return 0.0;
  }
  size_t diff = a > b ? a - b : b - a;
  return static_cast<double>(diff) / static_cast<double>(max_len);
}

vector<uint16> non_grease_cipher_sequence(const ParsedClientHello &hello) {
  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  vector<uint16> out;
  out.reserve(cipher_suites.size());
  for (auto cs : cipher_suites) {
    if (!is_grease_value(cs)) {
      out.push_back(cs);
    }
  }
  return out;
}

double composite_distance(const string &wire_x, const string &wire_y) {
  auto parsed_x = parse_tls_client_hello(wire_x).move_as_ok();
  auto parsed_y = parse_tls_client_hello(wire_y).move_as_ok();

  auto set_x = extension_set_non_grease_no_padding(parsed_x);
  auto set_y = extension_set_non_grease_no_padding(parsed_y);
  double jaccard = jaccard_distance(set_x, set_y);

  auto seq_x = non_grease_cipher_sequence(parsed_x);
  auto seq_y = non_grease_cipher_sequence(parsed_y);
  double kendall = kendall_tau_distance(seq_x, seq_y);

  double hamming = hamming_distance_u16(feature_bits(parsed_x), feature_bits(parsed_y));

  double length = length_delta_normalized(wire_x.size(), wire_y.size());

  return 0.3 * jaccard + 0.3 * kendall + 0.2 * hamming + 0.2 * length;
}

void run_cross_family_distance(BrowserProfile a, BrowserProfile b) {
  auto mode_a = ech_mode_for_profile(a);
  auto mode_b = ech_mode_for_profile(b);

  for (uint64 seed = 0; seed < kNightlyIterations; seed++) {
    auto wire_a = build_hello(a, mode_a, seed);
    auto wire_b = build_hello(b, mode_b, seed);
    double d = composite_distance(wire_a, wire_b);
    ASSERT_TRUE(d >= kCompositeDistanceFloor);
  }
}

TEST(TLS_NightlyCrossFamilyDistance, Chrome133VsFirefox148) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_cross_family_distance(BrowserProfile::Chrome133, BrowserProfile::Firefox148);
}

TEST(TLS_NightlyCrossFamilyDistance, Chrome133VsSafari26_3) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_cross_family_distance(BrowserProfile::Chrome133, BrowserProfile::Safari26_3);
}

TEST(TLS_NightlyCrossFamilyDistance, Firefox148VsSafari26_3) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_cross_family_distance(BrowserProfile::Firefox148, BrowserProfile::Safari26_3);
}

}  // namespace
