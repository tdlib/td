// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Structural validity, key material safety, and ML-KEM/X25519 stress tests.
// These tests verify TLS wire format correctness and cryptographic key material
// quality at scale to prevent DPI detection via malformed or weak key shares.

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <cstring>
#include <set>
#include <unordered_set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;
using namespace td::mtproto::test::fixtures;

const uint64 kCorpusIterations = spot_or_full_corpus_iterations();
constexpr uint64 kKeyMaterialIterations = kSpotIterations;
constexpr int32 kUnixTime = 1712345678;

uint64 quick_seed(uint64 iteration_index) {
  return corpus_seed_for_iteration(iteration_index, kQuickIterations);
}

uint64 corpus_seed(uint64 iteration_index) {
  return corpus_seed_for_iteration(iteration_index, kCorpusIterations);
}

uint64 key_material_seed(uint64 iteration_index) {
  return corpus_seed_for_iteration(iteration_index, kKeyMaterialIterations);
}

ParsedClientHello build_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  auto wire =
      build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

string build_wire(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
}

// -- TLS record layer structural validity --

TEST(StructuralKeyMaterialStress1k, AllProfilesAllEchRecordLayerIs0x16) {
  for (auto profile : all_profiles()) {
    for (auto ech_mode : {EchMode::Disabled, EchMode::Rfc9180Outer}) {
      if (ech_mode == EchMode::Rfc9180Outer && !profile_spec(profile).allows_ech) {
        continue;
      }
      for (uint64 seed = 0; seed < kQuickIterations; seed++) {
        ASSERT_EQ(0x16u, build_hello(profile, ech_mode, quick_seed(seed)).record_type);
      }
    }
  }
}

TEST(StructuralKeyMaterialStress1k, AllProfilesRecordVersionIs0x0301) {
  for (auto profile : all_profiles()) {
    for (uint64 seed = 0; seed < kQuickIterations; seed++) {
      ASSERT_EQ(0x0301u, build_hello(profile, EchMode::Disabled, quick_seed(seed)).record_legacy_version);
    }
  }
}

TEST(StructuralKeyMaterialStress1k, AllProfilesHandshakeTypeIs0x01) {
  for (auto profile : all_profiles()) {
    for (uint64 seed = 0; seed < kQuickIterations; seed++) {
      ASSERT_EQ(0x01u, build_hello(profile, EchMode::Disabled, quick_seed(seed)).handshake_type);
    }
  }
}

TEST(StructuralKeyMaterialStress1k, AllProfilesLegacyVersionIs0x0303) {
  for (auto profile : all_profiles()) {
    for (uint64 seed = 0; seed < kQuickIterations; seed++) {
      ASSERT_EQ(0x0303u, build_hello(profile, EchMode::Disabled, quick_seed(seed)).client_legacy_version);
    }
  }
}

TEST(StructuralKeyMaterialStress1k, AllProfilesSessionIdIs32Bytes) {
  for (auto profile : all_profiles()) {
    for (uint64 seed = 0; seed < kQuickIterations; seed++) {
      ASSERT_EQ(32u, build_hello(profile, EchMode::Disabled, quick_seed(seed)).session_id.size());
    }
  }
}

TEST(StructuralKeyMaterialStress1k, AllProfilesCompressionMethodsAreNull) {
  for (auto profile : all_profiles()) {
    for (uint64 seed = 0; seed < kQuickIterations; seed++) {
      auto hello = build_hello(profile, EchMode::Disabled, quick_seed(seed));
      // Compression methods should be exactly "\x00" (null compression only)
      ASSERT_EQ(1u, hello.compression_methods.size());
      ASSERT_EQ(0x00u, static_cast<uint8>(hello.compression_methods[0]));
    }
  }
}

TEST(StructuralKeyMaterialStress1k, AllProfilesNoDuplicateExtensionTypes) {
  for (auto profile : all_profiles()) {
    for (auto ech_mode : {EchMode::Disabled, EchMode::Rfc9180Outer}) {
      if (ech_mode == EchMode::Rfc9180Outer && !profile_spec(profile).allows_ech) {
        continue;
      }
      for (uint64 seed = 0; seed < kQuickIterations; seed++) {
        auto hello = build_hello(profile, ech_mode, quick_seed(seed));
        std::unordered_set<uint16> seen;
        for (const auto &ext : hello.extensions) {
          ASSERT_TRUE(seen.insert(ext.type).second);
        }
      }
    }
  }
}

// -- Record length consistency --

TEST(StructuralKeyMaterialStress1k, RecordLengthMatchesWirePayload) {
  for (auto profile : all_profiles()) {
    for (uint64 seed = 0; seed < kQuickIterations; seed++) {
      auto wire = build_wire(profile, EchMode::Disabled, quick_seed(seed));
      ASSERT_TRUE(wire.size() >= 5);
      auto record_length =
          (static_cast<uint16>(static_cast<uint8>(wire[3])) << 8) | static_cast<uint16>(static_cast<uint8>(wire[4]));
      ASSERT_EQ(wire.size() - 5, static_cast<size_t>(record_length));
    }
  }
}

TEST(StructuralKeyMaterialStress1k, HandshakeLengthMatchesPayload) {
  for (auto profile : all_profiles()) {
    for (uint64 seed = 0; seed < kQuickIterations; seed++) {
      auto hello = build_hello(profile, EchMode::Disabled, quick_seed(seed));
      auto wire = build_wire(profile, EchMode::Disabled, quick_seed(seed));
      // Handshake starts at offset 5; type(1) + length(3) = 4 header bytes
      ASSERT_TRUE(wire.size() >= 9);
      auto handshake_length = (static_cast<uint32>(static_cast<uint8>(wire[6])) << 16) |
                              (static_cast<uint32>(static_cast<uint8>(wire[7])) << 8) |
                              static_cast<uint32>(static_cast<uint8>(wire[8]));
      ASSERT_EQ(wire.size() - 9, handshake_length);
    }
  }
}

// -- X25519 key share validity --

TEST(StructuralKeyMaterialStress1k, X25519KeyShareIsValidCurvePoint) {
  for (auto profile :
       {BrowserProfile::Chrome133, BrowserProfile::Firefox148, BrowserProfile::Safari26_3, BrowserProfile::IOS14}) {
    for (uint64 seed = 0; seed < kKeyMaterialIterations; seed++) {
      auto hello = build_hello(profile, EchMode::Disabled, key_material_seed(seed));
      bool found_x25519 = false;
      for (const auto &entry : hello.key_share_entries) {
        if (entry.group == kX25519Group) {
          found_x25519 = true;
          ASSERT_EQ(32u, entry.key_data.size());
          ASSERT_TRUE(is_valid_curve25519_public_coordinate(entry.key_data));
        }
      }
      ASSERT_TRUE(found_x25519);
    }
  }
}

TEST(StructuralKeyMaterialStress1k, X25519KeyShareNeverAllZeros) {
  for (uint64 seed = 0; seed < kKeyMaterialIterations; seed++) {
    auto hello = build_hello(BrowserProfile::Chrome133, EchMode::Disabled, key_material_seed(seed));
    for (const auto &entry : hello.key_share_entries) {
      if (entry.group == kX25519Group) {
        bool all_zero = true;
        for (size_t i = 0; i < entry.key_data.size(); i++) {
          if (entry.key_data[i] != '\0') {
            all_zero = false;
            break;
          }
        }
        ASSERT_FALSE(all_zero);
      }
    }
  }
}

TEST(StructuralKeyMaterialStress1k, X25519KeyShareNeverAllOnes) {
  for (uint64 seed = 0; seed < kKeyMaterialIterations; seed++) {
    auto hello = build_hello(BrowserProfile::Chrome133, EchMode::Disabled, key_material_seed(seed));
    for (const auto &entry : hello.key_share_entries) {
      if (entry.group == kX25519Group) {
        bool all_ones = true;
        for (size_t i = 0; i < entry.key_data.size(); i++) {
          if (static_cast<uint8>(entry.key_data[i]) != 0xFF) {
            all_ones = false;
            break;
          }
        }
        ASSERT_FALSE(all_ones);
      }
    }
  }
}

// -- PQ key share vs X25519 isolation --

TEST(StructuralKeyMaterialStress1k, PqKeyShareDiffersFromX25519KeyShare) {
  for (uint64 seed = 0; seed < kKeyMaterialIterations; seed++) {
    auto hello = build_hello(BrowserProfile::Chrome133, EchMode::Disabled, key_material_seed(seed));
    Slice pq_data;
    Slice x25519_data;
    for (const auto &entry : hello.key_share_entries) {
      if (entry.group == kPqHybridGroup) {
        pq_data = entry.key_data;
      }
      if (entry.group == kX25519Group) {
        x25519_data = entry.key_data;
      }
    }
    ASSERT_FALSE(pq_data.empty());
    ASSERT_FALSE(x25519_data.empty());
    // PQ is 1216 bytes, X25519 is 32 — different lengths guarantees inequality,
    // but verify the first 32 bytes of PQ are not identical to X25519 (copy-paste bug guard)
    ASSERT_TRUE(pq_data.substr(0, 32).str() != x25519_data.str());
  }
}

// -- ML-KEM coefficient validity --
// The builder generates ML-KEM coefficients via bounded(3329).
// The resulting 1184-byte blob encodes 384 pairs of 12-bit values (a << 0 | b << 12).
// Each pair of coefficients (a, b) must satisfy 0 <= a < 3329 and 0 <= b < 3329.

TEST(StructuralKeyMaterialStress1k, MlKemCoefficientsWithinValidRange) {
  for (uint64 seed = 0; seed < kKeyMaterialIterations; seed++) {
    auto hello = build_hello(BrowserProfile::Chrome133, EchMode::Disabled, key_material_seed(seed));
    for (const auto &entry : hello.key_share_entries) {
      if (entry.group != kPqHybridGroup) {
        continue;
      }
      // First 1184 bytes of PQ hybrid key share is ML-KEM-768 public key
      ASSERT_TRUE(entry.key_data.size() >= 1184u + 32u);
      auto mlkem = entry.key_data.substr(0, 1184);
      // Decode packed 12-bit coefficients: every 3 bytes encode 2 coefficients
      for (size_t i = 0; i + 2 < 1152; i += 3) {
        auto b0 = static_cast<uint32>(static_cast<uint8>(mlkem[i]));
        auto b1 = static_cast<uint32>(static_cast<uint8>(mlkem[i + 1]));
        auto b2 = static_cast<uint32>(static_cast<uint8>(mlkem[i + 2]));
        uint32 coeff_a = b0 | ((b1 & 0x0F) << 8);
        uint32 coeff_b = (b1 >> 4) | (b2 << 4);
        ASSERT_TRUE(coeff_a < 3329u);
        ASSERT_TRUE(coeff_b < 3329u);
      }
    }
  }
}

// -- PQ key share entropy --

TEST(StructuralKeyMaterialStress1k, PqKeySharesAreUniqueAcrossSeeds) {
  std::set<string> pq_keys;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_hello(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(seed));
    for (const auto &entry : hello.key_share_entries) {
      if (entry.group == kPqHybridGroup) {
        pq_keys.insert(entry.key_data.str());
      }
    }
  }
  ASSERT_EQ(kCorpusIterations, pq_keys.size());
}

// -- ECH enc key is valid X25519 coordinate --

TEST(StructuralKeyMaterialStress1k, EchEncKeyIsValidX25519Coordinate) {
  for (uint64 seed = 0; seed < kKeyMaterialIterations; seed++) {
    auto hello = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, key_material_seed(seed));
    ASSERT_EQ(32u, hello.ech_actual_enc_length);
    ASSERT_TRUE(is_valid_curve25519_public_coordinate(hello.ech_enc));
  }
}

// -- ECH structural fields --

TEST(StructuralKeyMaterialStress1k, ChromeEchFieldsMatchExpectedValues) {
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    auto hello = build_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, quick_seed(seed));
    ASSERT_EQ(0x00u, hello.ech_outer_type);  // outer
    ASSERT_EQ(0x0001u, hello.ech_kdf_id);    // HKDF-SHA256
    ASSERT_EQ(32u, hello.ech_actual_enc_length);
    // AEAD must be a valid AEAD algorithm (0x0001=AES-128-GCM or 0x0002=AES-256-GCM or 0x0003=ChaCha20Poly1305)
    ASSERT_TRUE(hello.ech_aead_id >= 0x0001u && hello.ech_aead_id <= 0x0003u);
  }
}

// -- Supported groups consistency: key_share groups must be subset of supported_groups --

TEST(StructuralKeyMaterialStress1k, KeyShareGroupsAreSubsetOfSupportedGroups) {
  for (auto profile : all_profiles()) {
    for (uint64 seed = 0; seed < kQuickIterations; seed++) {
      auto hello = build_hello(profile, EchMode::Disabled, quick_seed(seed));
      std::unordered_set<uint16> sg(hello.supported_groups.begin(), hello.supported_groups.end());
      for (const auto &entry : hello.key_share_entries) {
        ASSERT_TRUE(sg.count(entry.group) != 0 || is_grease_value(entry.group));
      }
    }
  }
}

// -- Pathological input stress tests --

TEST(StructuralKeyMaterialStress1k, MaxLengthDomainDoesNotCrashOrCorrupt) {
  // ProxySecret::MAX_DOMAIN_LENGTH is checked in the builder
  string long_domain(253, 'a');
  long_domain += ".com";
  for (uint64 seed = 0; seed < 64; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile(long_domain, "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(0x0303u, parsed.ok().client_legacy_version);
  }
}

TEST(StructuralKeyMaterialStress1k, ShortDomainDoesNotCrashOrCorrupt) {
  for (uint64 seed = 0; seed < 64; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("a.b", "0123456789secret", kUnixTime, BrowserProfile::Chrome133,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
  }
}

// -- Fixed-value RNG adversarial stress test --

class FixedByteRng final : public IRng {
 public:
  explicit FixedByteRng(uint8 byte_value) : byte_value_(byte_value) {
  }
  void fill_secure_bytes(MutableSlice dest) final {
    std::memset(dest.begin(), byte_value_, dest.size());
  }
  uint32 secure_uint32() final {
    return static_cast<uint32>(byte_value_) * 0x01010101u;
  }
  uint32 bounded(uint32 n) final {
    CHECK(n > 0);
    return secure_uint32() % n;
  }

 private:
  uint8 byte_value_;
};

TEST(StructuralKeyMaterialStress1k, AllZerosRngProducesValidWire) {
  FixedByteRng rng(0x00);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_EQ(0x0303u, parsed.ok().client_legacy_version);
}

TEST(StructuralKeyMaterialStress1k, AllOnesRngProducesValidWire) {
  FixedByteRng rng(0xFF);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
}

TEST(StructuralKeyMaterialStress1k, AllZerosRngProducesValidFirefoxWire) {
  FixedByteRng rng(0x00);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Firefox148, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
}

TEST(StructuralKeyMaterialStress1k, AllZerosRngProducesValidSafariWire) {
  FixedByteRng rng(0x00);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Safari26_3, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
}

TEST(StructuralKeyMaterialStress1k, AllProfilesWithEchProduceParseableWire) {
  for (auto profile : all_profiles()) {
    if (!profile_spec(profile).allows_ech) {
      continue;
    }
    for (uint64 seed = 0; seed < kQuickIterations; seed++) {
      auto wire = build_wire(profile, EchMode::Rfc9180Outer, quick_seed(seed));
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
    }
  }
}

}  // namespace
