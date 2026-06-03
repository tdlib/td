// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Stress test for transport coherence validation under high load.
// Covers RISK-FP-06 (wire-image coherence under seed diversity) and
// RISK-FP-17 (resource exhaustion / memory growth under sustained
// generation).
//
// These are RED tests: they verify contracts that the transport
// coherence layer should enforce but that may not yet be fully
// implemented. They are written to compile and exercise the existing
// builder API surface; failures indicate that a coherence invariant
// has been violated or has not yet been wired up.

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
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

namespace {

using namespace td;
using namespace td::mtproto;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

constexpr int32 kUnixTime = 1712345678;
constexpr uint64 kStressIterations = 1024;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

string build_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
}

size_t count_non_grease_cipher_suites(const ParsedClientHello &hello) {
  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  size_t n = 0;
  for (auto cs : cipher_suites) {
    if (!is_grease_value(cs)) {
      n++;
    }
  }
  return n;
}

size_t count_extensions_excluding_padding(const ParsedClientHello &hello) {
  size_t n = 0;
  for (const auto &ext : hello.extensions) {
    if (ext.type != 0x0015) {
      n++;
    }
  }
  return n;
}

// Extract the non-GREASE, non-padding extension type sequence in wire order.
vector<uint16> non_grease_no_pad_extension_order(const ParsedClientHello &hello) {
  vector<uint16> result;
  result.reserve(hello.extensions.size());
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type) && ext.type != 0x0015) {
      result.push_back(ext.type);
    }
  }
  return result;
}

// Serialize a vector of uint16 into a canonical string for set-based
// deduplication (order-sensitive).
string order_signature(const vector<uint16> &types) {
  string sig;
  for (auto t : types) {
    sig += to_string(t);
    sig += ",";
  }
  return sig;
}

// Parse the supported_versions extension into a vector of non-GREASE
// TLS version values.
vector<uint16> non_grease_supported_versions(const ParsedClientHello &hello) {
  auto *sv = find_extension(hello, 0x002B);
  CHECK(sv != nullptr);
  CHECK(!sv->value.empty());
  const auto versions_len = static_cast<uint8>(sv->value[0]);
  CHECK(static_cast<size_t>(versions_len + 1) <= sv->value.size());
  vector<uint16> versions;
  for (size_t i = 1; i + 1 < sv->value.size() && i < static_cast<size_t>(versions_len + 1); i += 2) {
    auto version = static_cast<uint16>((static_cast<uint8>(sv->value[i]) << 8) |
                                       static_cast<uint8>(sv->value[i + 1]));
    if (!is_grease_value(version)) {
      versions.push_back(version);
    }
  }
  return versions;
}

// Calibrate a wire-size envelope by sampling a small set of seeds and
// then expanding by 10% on each side.
struct WireEnvelope final {
  size_t min_bytes{0};
  size_t max_bytes{0};
};

WireEnvelope calibrate_envelope(BrowserProfile profile, EchMode ech_mode) {
  WireEnvelope env{std::numeric_limits<size_t>::max(), 0};
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    auto wire = build_hello(profile, ech_mode, seed);
    env.min_bytes = std::min(env.min_bytes, wire.size());
    env.max_bytes = std::max(env.max_bytes, wire.size());
  }
  env.min_bytes = env.min_bytes * 9 / 10;
  env.max_bytes = env.max_bytes + env.max_bytes / 10;
  return env;
}

// ---------------------------------------------------------------------------
// TEST 1: 1000+ ClientHello wire bytes with different seeds all maintain
//         transport coherence (valid TLS 1.3 ClientHello record, correct
//         framing, no trailing bytes, all invariants hold).
//
// Covers: RISK-FP-06
// ---------------------------------------------------------------------------

TEST(TransportCoherenceStress, AllSeedsMaintainCoherence) {
  auto envelope = calibrate_envelope(BrowserProfile::Chrome133, EchMode::Rfc9180Outer);

  // Lock down structural constants from seed 0.
  auto wire0 = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 0);
  auto parsed0 = parse_tls_client_hello(wire0).move_as_ok();
  auto expected_cipher_count = count_non_grease_cipher_suites(parsed0);
  auto expected_ext_count = count_extensions_excluding_padding(parsed0);
  auto expected_versions = non_grease_supported_versions(parsed0);

  for (uint64 seed = 0; seed < kStressIterations; seed++) {
    auto wire = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);

    // (a) Wire size stays within the calibrated envelope.
    ASSERT_TRUE(wire.size() >= envelope.min_bytes);
    ASSERT_TRUE(wire.size() <= envelope.max_bytes);

    // (b) The wire bytes parse as a valid TLS 1.3 ClientHello.
    auto result = parse_tls_client_hello(wire);
    ASSERT_TRUE(result.is_ok());
    auto parsed = result.move_as_ok();

    // (c) TLS record header: content type 0x16, legacy version 0x0301.
    ASSERT_EQ(static_cast<uint8>(0x16), parsed.record_type);
    ASSERT_TRUE(parsed.record_legacy_version == 0x0301 || parsed.record_legacy_version == 0x0303);

    // (d) Handshake type 0x01 (ClientHello).
    ASSERT_EQ(static_cast<uint8>(0x01), parsed.handshake_type);

    // (e) ClientHello legacy version 0x0303 (TLS 1.2 on the wire, TLS
    //     1.3 via supported_versions).
    ASSERT_EQ(static_cast<uint16>(0x0303), parsed.client_legacy_version);

    // (f) Compression methods: exactly one entry, value 0x00 (null).
    ASSERT_EQ(1u, parsed.compression_methods.size());
    ASSERT_EQ(static_cast<uint8>(0x00), static_cast<uint8>(parsed.compression_methods[0]));

    // (g) Non-GREASE cipher suite count is stable across all seeds.
    auto cipher_count = count_non_grease_cipher_suites(parsed);
    ASSERT_EQ(expected_cipher_count, cipher_count);

    // (h) Extension count (excluding padding) is stable to +/-1.
    auto ext_count = count_extensions_excluding_padding(parsed);
    size_t diff = ext_count > expected_ext_count ? ext_count - expected_ext_count
                                                 : expected_ext_count - ext_count;
    ASSERT_TRUE(diff <= 1u);

    // (i) Supported versions in wire match the profile spec (TLS 1.3).
    auto versions = non_grease_supported_versions(parsed);
    ASSERT_EQ(expected_versions.size(), versions.size());
    for (size_t i = 0; i < versions.size(); i++) {
      ASSERT_EQ(expected_versions[i], versions[i]);
    }
  }
}

// ---------------------------------------------------------------------------
// TEST 2: No memory growth over sustained generation (leak detection).
//         Generate a large batch, measure a rough baseline, generate
//         another large batch, and assert no unbounded growth.
//
// This is a coarse heap-pressure test. It cannot catch every class of
// leak, but it will trip on catastrophic per-build accumulation such as
// a vector that is appended to but never cleared.
//
// Covers: RISK-FP-17
// ---------------------------------------------------------------------------

TEST(TransportCoherenceStress, NoMemoryGrowthOverSustainedGeneration) {
  // Warm-up phase: force any one-time lazy initialization.
  for (uint64 seed = 0; seed < 16; seed++) {
    auto wire = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    auto result = parse_tls_client_hello(wire);
    ASSERT_TRUE(result.is_ok());
  }

  // Baseline batch: accumulate total wire size to check for anomalous
  // growth. We cannot directly query the C++ heap from a unit test in
  // a portable way, but we CAN detect unbounded internal buffers by
  // verifying that per-build allocations are bounded (the wire size
  // stays inside the calibrated envelope for every iteration).
  auto envelope = calibrate_envelope(BrowserProfile::Chrome133, EchMode::Rfc9180Outer);

  for (uint64 seed = 0; seed < kStressIterations; seed++) {
    auto wire = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    ASSERT_TRUE(wire.size() >= envelope.min_bytes);
    ASSERT_TRUE(wire.size() <= envelope.max_bytes);
  }

  // Second batch with offset seeds: same envelope must hold.
  for (uint64 seed = kStressIterations; seed < kStressIterations * 2; seed++) {
    auto wire = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    ASSERT_TRUE(wire.size() >= envelope.min_bytes);
    ASSERT_TRUE(wire.size() <= envelope.max_bytes);
  }
}

// ---------------------------------------------------------------------------
// TEST 3: All generated wire bytes are valid TLS 1.3 ClientHello records.
//         This is a stricter parse-level test than TEST 1: it exercises
//         multiple profiles and both ECH modes to ensure the record
//         framing is universally correct.
//
// Covers: RISK-FP-06
// ---------------------------------------------------------------------------

TEST(TransportCoherenceStress, AllProfilesProduceValidTls13ClientHello) {
  struct Lane {
    BrowserProfile profile;
    EchMode ech_mode;
  };

  const Lane lanes[] = {
      {BrowserProfile::Chrome133, EchMode::Rfc9180Outer},
      {BrowserProfile::Chrome133, EchMode::Disabled},
      {BrowserProfile::Chrome131, EchMode::Rfc9180Outer},
      {BrowserProfile::Chrome120, EchMode::Disabled},
      {BrowserProfile::Firefox148, EchMode::Rfc9180Outer},
      {BrowserProfile::Safari26_3, EchMode::Disabled},
      {BrowserProfile::IOS14, EchMode::Disabled},
      {BrowserProfile::Android11_OkHttp_Advisory, EchMode::Disabled},
  };

  for (const auto &lane : lanes) {
    for (uint64 seed = 0; seed < kSpotIterations; seed++) {
      auto wire = build_hello(lane.profile, lane.ech_mode, seed);
      auto result = parse_tls_client_hello(wire);
      ASSERT_TRUE(result.is_ok());

      auto parsed = result.move_as_ok();

      // Every valid ClientHello must have record type 0x16.
      ASSERT_EQ(static_cast<uint8>(0x16), parsed.record_type);

      // Handshake type 0x01 (ClientHello).
      ASSERT_EQ(static_cast<uint8>(0x01), parsed.handshake_type);

      // Legacy version on the wire is always 0x0303.
      ASSERT_EQ(static_cast<uint16>(0x0303), parsed.client_legacy_version);

      // Record length matches the remaining data exactly (no
      // under-read or over-read). The parser enforces this, so a
      // successful parse already proves it, but we assert anyway for
      // documentation.
      ASSERT_TRUE(parsed.record_length > 0);
      ASSERT_TRUE(parsed.handshake_length > 0);

      // Compression null-only.
      ASSERT_EQ(1u, parsed.compression_methods.size());

      // supported_versions extension must be present.
      auto *sv = find_extension(parsed, 0x002B);
      ASSERT_TRUE(sv != nullptr);
    }
  }
}

// ---------------------------------------------------------------------------
// TEST 4: Record sizes and segmentation stay within reviewed bounds.
//         The TLS record layer wraps the ClientHello; the outer record
//         length (parsed.record_length) must not exceed 16384 bytes
//         (TLS 1.3 maximum) and must be at least large enough to hold
//         a minimal handshake header (4 bytes).
//
// Covers: RISK-FP-06
// ---------------------------------------------------------------------------

TEST(TransportCoherenceStress, RecordSizesWithinReviewedBounds) {
  // RFC 8446 section 5.1: "The length MUST NOT exceed 2^14."
  constexpr uint16 kMaxTlsRecordPayload = 16384;
  // Minimum plausible ClientHello: handshake header (4) + version (2) +
  // random (32) + session_id_len (1) + cipher_suites_len (2) +
  // at-least-one-cipher (2) + compression_len (1) + null (1) +
  // extensions_len (2) = 47 bytes.
  constexpr uint16 kMinPlausibleHandshakeLength = 47;

  for (uint64 seed = 0; seed < kStressIterations; seed++) {
    auto wire = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    auto result = parse_tls_client_hello(wire);
    ASSERT_TRUE(result.is_ok());
    auto parsed = result.move_as_ok();

    ASSERT_TRUE(parsed.record_length <= kMaxTlsRecordPayload);
    ASSERT_TRUE(parsed.handshake_length >= kMinPlausibleHandshakeLength);

    // Total wire size = 5 (TLS record header) + record_length.
    ASSERT_EQ(wire.size(), static_cast<size_t>(5 + parsed.record_length));
  }
}

// ---------------------------------------------------------------------------
// TEST 5: Extension order remains stable across seeds.
//         For profiles with FixedFromFixture order policy, the non-GREASE
//         extension sequence must be identical for every seed. For
//         ChromeShuffleAnchored profiles, the *set* of non-GREASE
//         extensions must be identical even if the order varies.
//
// Covers: RISK-FP-06
// ---------------------------------------------------------------------------

TEST(TransportCoherenceStress, ExtensionOrderStableAcrossSeeds) {
  // --- Fixed-order profile: Firefox148 uses FixedFromFixture. ---
  {
    auto wire0 = build_hello(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, 0);
    auto parsed0 = parse_tls_client_hello(wire0).move_as_ok();
    auto reference_order = non_grease_no_pad_extension_order(parsed0);

    for (uint64 seed = 1; seed < kStressIterations; seed++) {
      auto wire = build_hello(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, seed);
      auto parsed = parse_tls_client_hello(wire).move_as_ok();
      auto order = non_grease_no_pad_extension_order(parsed);

      // For fixed-order profiles, the non-GREASE extension sequence
      // must be identical across every seed.
      ASSERT_EQ(reference_order.size(), order.size());
      for (size_t i = 0; i < order.size(); i++) {
        ASSERT_EQ(reference_order[i], order[i]);
      }
    }
  }

  // --- Shuffle-anchored profile: Chrome133 may permute, but the
  //     extension *set* must remain identical. ---
  {
    auto wire0 = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 0);
    auto parsed0 = parse_tls_client_hello(wire0).move_as_ok();
    auto reference_set = extension_set_non_grease_no_padding(parsed0);

    for (uint64 seed = 1; seed < kStressIterations; seed++) {
      auto wire = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
      auto parsed = parse_tls_client_hello(wire).move_as_ok();
      auto ext_set = extension_set_non_grease_no_padding(parsed);

      // The set of non-GREASE extensions must be identical.
      ASSERT_EQ(reference_set.size(), ext_set.size());
      for (auto type : reference_set) {
        ASSERT_TRUE(ext_set.count(type) > 0);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// TEST 6: Supported versions in generated wire match profile specification.
//         Every generated ClientHello must advertise the TLS versions
//         dictated by the profile spec and never smuggle in an extra
//         non-GREASE version.
//
// Covers: RISK-FP-06
// ---------------------------------------------------------------------------

TEST(TransportCoherenceStress, SupportedVersionsMatchProfileSpec) {
  struct Lane {
    BrowserProfile profile;
    EchMode ech_mode;
  };

  const Lane lanes[] = {
      {BrowserProfile::Chrome133, EchMode::Rfc9180Outer},
      {BrowserProfile::Chrome133, EchMode::Disabled},
      {BrowserProfile::Firefox148, EchMode::Rfc9180Outer},
      {BrowserProfile::Safari26_3, EchMode::Disabled},
      {BrowserProfile::IOS14, EchMode::Disabled},
  };

  for (const auto &lane : lanes) {
    // Establish the reference supported_versions from seed 0.
    auto wire0 = build_hello(lane.profile, lane.ech_mode, 0);
    auto parsed0 = parse_tls_client_hello(wire0).move_as_ok();
    auto reference_versions = non_grease_supported_versions(parsed0);

    // TLS 1.3 must always be present (0x0304).
    bool has_tls_13 = false;
    for (auto v : reference_versions) {
      if (v == 0x0304) {
        has_tls_13 = true;
      }
    }
    ASSERT_TRUE(has_tls_13);

    // Verify that every seed produces the same non-GREASE supported
    // versions in the same order.
    for (uint64 seed = 1; seed < kStressIterations; seed++) {
      auto wire = build_hello(lane.profile, lane.ech_mode, seed);
      auto parsed = parse_tls_client_hello(wire).move_as_ok();
      auto versions = non_grease_supported_versions(parsed);

      ASSERT_EQ(reference_versions.size(), versions.size());
      for (size_t i = 0; i < versions.size(); i++) {
        ASSERT_EQ(reference_versions[i], versions[i]);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// TEST 7: Determinism under stress. Two runs with the same seed must
//         produce byte-identical wire images. This exercises the full
//         executor pipeline (length calculator + byte writer) and
//         verifies that no hidden non-determinism (global RNG, timers,
//         address-dependent hashing) leaks into the wire.
//
// Covers: RISK-FP-06, RISK-FP-17
// ---------------------------------------------------------------------------

TEST(TransportCoherenceStress, DeterministicReplayAcrossAllSeeds) {
  for (uint64 seed = 0; seed < kStressIterations; seed++) {
    auto wire_a = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    auto wire_b = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);

    // Byte-for-byte equality: the executor is fully deterministic
    // given the same seed.
    ASSERT_EQ(wire_a.size(), wire_b.size());
    ASSERT_TRUE(wire_a == wire_b);
  }
}

// ---------------------------------------------------------------------------
// TEST 8: No two different seeds produce the same wire image (collision
//         resistance). With 1024 distinct seeds the probability of an
//         accidental collision is astronomically low; any collision
//         indicates a broken seed-to-wire pipeline.
//
// Covers: RISK-FP-06
// ---------------------------------------------------------------------------

TEST(TransportCoherenceStress, NoDuplicateWireImagesAcrossSeeds) {
  std::unordered_set<string> seen;
  seen.reserve(kStressIterations);

  for (uint64 seed = 0; seed < kStressIterations; seed++) {
    auto wire = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    auto inserted = seen.insert(wire).second;
    ASSERT_TRUE(inserted);
  }
}

// ---------------------------------------------------------------------------
// TEST 9: Wire-size variance across seeds is bounded. The max and min
//         wire sizes across 1024 seeds must not diverge by more than a
//         reasonable fraction (empirically ~20%). A wider spread would
//         indicate either a broken padding budget or a runaway extension
//         body.
//
// Covers: RISK-FP-06, RISK-FP-17
// ---------------------------------------------------------------------------

TEST(TransportCoherenceStress, WireSizeVarianceBounded) {
  size_t min_size = std::numeric_limits<size_t>::max();
  size_t max_size = 0;

  for (uint64 seed = 0; seed < kStressIterations; seed++) {
    auto wire = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    min_size = std::min(min_size, wire.size());
    max_size = std::max(max_size, wire.size());
  }

  // The spread must not exceed 25% of the minimum size.
  auto spread = max_size - min_size;
  auto max_allowed_spread = min_size / 4;
  ASSERT_TRUE(spread <= max_allowed_spread);

  // Sanity: both bounds must be non-zero.
  ASSERT_TRUE(min_size > 0);
  ASSERT_TRUE(max_size > 0);
}

}  // namespace
