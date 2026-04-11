// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
//
// REG-5 — X25519MLKEM768 hybrid key_share wire format invariants.
//
// IANA codepoint 0x11EC (X25519MLKEM768) describes a hybrid post-quantum +
// classical TLS 1.3 key exchange. Per draft-kwiatkowski-tls-ecdhe-mlkem and
// the bundled uTLS reference implementation (`HelloChrome_131` /
// `HelloChrome_133`), the hybrid public key serialised on the wire is the
// concatenation of:
//
//   ML-KEM-768 public key   1184 bytes  (k * n * log2(q)/8 + 32-byte rho)
//   X25519 public key         32 bytes  (clamped Curve25519 x-coordinate)
//   --------------------------------
//   total                   1216 bytes  (= 0x04C0)
//
// Real Chrome 131 / Chrome 133 / Firefox 148 wire-images therefore have a
// `key_share` entry whose 2-byte length field equals **0x04C0**, not
// 0x04A0 (which would be ML-KEM-768 alone, missing the X25519 hybrid part).
//
// The pre-c7f013608 builder used three explicit ops in sequence —
// `Op::pq_key_share() + Op::ml_kem_768_key() + Op::key()` — to emit
// header + ML-KEM-768 + X25519. The c7f013608 refactor collapsed this into
// a single `X25519MlKem768KeyShareEntry` executor op that emitted only the
// ML-KEM-768 portion and declared a length field of 1184 bytes, dropping
// the trailing X25519 component entirely. The wire-image is therefore
// neither a valid hybrid X25519MLKEM768 public key nor a valid ML-KEM-768
// public key on its own — it is a unique malformed signature.
//
// Adversarial scenarios this suite catches:
//
//   C1. Wrong declared length. Real wire is 0x04C0; bug emits 0x04A0.
//
//   C2. Missing X25519 trailer. The last 32 bytes of the entry must be a
//       valid Curve25519 x-coordinate; without the trailer the entry ends
//       abruptly mid-ML-KEM data and any quadratic-residue check on the
//       last 32 bytes will *probably* fail (and the test asserts it
//       *always* fails for the bug scenario, by exercising many seeds).
//
//   C3. Hybrid order swap. The bytes must be ML-KEM-768 *then* X25519,
//       not the other way round; reversing them is itself a fingerprint.
//
//   C4. Length field smaller than data. Even if a future bug writes
//       1216 bytes but declares 1184, the wire is malformed and any
//       strict TLS 1.3 parser rejects it.
//
// The PQ hybrid format is exercised by every PQ-bearing profile
// (Chrome 131, Chrome 133, Firefox 148, Firefox 149_macOS). Apple TLS
// (Safari 26.3, iOS 14) MUST NOT carry this entry at all — that
// invariant is enforced separately by
// `test_profile_spec_pq_consistency_invariants.cpp`.

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"

#include <vector>

namespace {

using td::mtproto::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::is_valid_curve25519_public_coordinate;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::ParsedClientHello;
using td::mtproto::test::ParsedKeyShareEntry;
using td::mtproto::test::fixtures::kPqHybridGroup;
using td::mtproto::test::fixtures::kPqHybridKeyShareLength;
using td::mtproto::test::fixtures::kX25519KeyShareLength;

constexpr td::int32 kFixedUnixTime = 1712345678;
constexpr td::Slice kSecret = td::Slice("0123456789secret");
constexpr td::Slice kHost = td::Slice("www.google.com");

constexpr td::uint16 kMlKem768PublicKeyLength = 1184;
constexpr td::uint16 kHybridTotalLength = 1216;
constexpr td::uint16 kHybridLengthOnWire = 0x04C0;

static_assert(kHybridTotalLength == kHybridLengthOnWire,
              "Wire constant must match canonical hybrid length");
static_assert(kHybridTotalLength == kPqHybridKeyShareLength,
              "Fixture constant drift would mask the regression");
static_assert(kHybridTotalLength == kMlKem768PublicKeyLength + kX25519KeyShareLength,
              "Hybrid length must equal ML-KEM-768 + X25519 byte counts");

const std::vector<BrowserProfile> &profiles_with_pq_hybrid_key_share() {
  static const std::vector<BrowserProfile> kProfiles = {
      BrowserProfile::Chrome133,
      BrowserProfile::Chrome131,
      BrowserProfile::Firefox148,
      BrowserProfile::Firefox149_MacOS26_3,
      // Apple TLS family on iOS 26.x adopted X25519MLKEM768. Real
      // captures under test/analysis/fixtures/clienthello/ios/ confirm
      // both Safari 26.3/26.4 and Chrome 147 on iOS 26.4 emit the
      // hybrid key share entry.
      BrowserProfile::Safari26_3,
      BrowserProfile::IOS14,
  };
  return kProfiles;
}

// `ParsedClientHello` carries non-owning `Slice`s into the wire buffer it
// was parsed from. The owning wrapper keeps the wire alive for the
// lifetime of the parse result.
struct OwnedParsedClientHello final {
  std::string wire;
  ParsedClientHello hello;
};

OwnedParsedClientHello build_with_pq(BrowserProfile profile, td::uint64 seed) {
  MockRng rng(seed);
  OwnedParsedClientHello result;
  result.wire = build_tls_client_hello_for_profile(kHost.str(), kSecret, kFixedUnixTime, profile, EchMode::Disabled,
                                                   rng);
  auto parsed = parse_tls_client_hello(result.wire);
  if (parsed.is_error()) {
    LOG(ERROR) << "PQ wire parse failed for profile " << static_cast<int>(profile) << " seed " << seed << ": "
               << parsed.error();
  }
  CHECK(parsed.is_ok());
  result.hello = parsed.move_as_ok();
  return result;
}

const ParsedKeyShareEntry *find_pq_hybrid_entry(const ParsedClientHello &hello) {
  for (const auto &entry : hello.key_share_entries) {
    if (entry.group == kPqHybridGroup) {
      return &entry;
    }
  }
  return nullptr;
}

// C1 + C4 — Every PQ-bearing profile must declare the wire length
// 0x04C0 and emit exactly 1216 bytes of key data.
TEST(PqHybridKeyShareFormat, EveryPqProfileDeclaresAndEmits1216BytesForHybridEntry) {
  for (auto profile : profiles_with_pq_hybrid_key_share()) {
    for (td::uint64 seed = 0; seed < 16; seed++) {
      auto owned = build_with_pq(profile, seed);
      const auto *entry = find_pq_hybrid_entry(owned.hello);
      ASSERT_TRUE(entry != nullptr);

      ASSERT_EQ(kHybridTotalLength, entry->key_length);
      ASSERT_EQ(static_cast<size_t>(kHybridTotalLength), entry->key_data.size());
    }
  }
}

// C2 — The trailing 32 bytes of the hybrid entry must be a valid
// Curve25519 x-coordinate. The parser already enforces this; the test
// asserts the precondition holds across many seeds and profiles, so any
// regression that drops the X25519 trailer is caught loud and fast.
TEST(PqHybridKeyShareFormat, HybridEntryTrailingX25519IsValidCurve25519CoordinateForAllSeeds) {
  for (auto profile : profiles_with_pq_hybrid_key_share()) {
    for (td::uint64 seed = 0; seed < 32; seed++) {
      auto owned = build_with_pq(profile, seed);
      const auto *entry = find_pq_hybrid_entry(owned.hello);
      ASSERT_TRUE(entry != nullptr);

      ASSERT_EQ(static_cast<size_t>(kHybridTotalLength), entry->key_data.size());
      auto x25519_tail =
          entry->key_data.substr(entry->key_data.size() - kX25519KeyShareLength, kX25519KeyShareLength);
      ASSERT_TRUE(is_valid_curve25519_public_coordinate(x25519_tail));
    }
  }
}

// C2b — Statistical robustness against the regression scenario "use raw
// random bytes for the X25519 trailer". A uniformly random 32-byte
// sequence is a valid Curve25519 x-coordinate only ~50% of the time
// (those where y^2 = x^3 + 486662*x^2 + x is a quadratic residue mod
// p25519). The rejection-sampling helper `store_x25519_key_share()`
// guarantees 100%. This test asserts that a *very large* sample of seeds
// — 256 — produces 0 invalid trailers, an outcome whose probability under
// the bug scenario is approximately (1/2)^256 ≈ 0.
TEST(PqHybridKeyShareFormat, AcrossWideSeedSweepEveryHybridX25519TrailerIsValidCoordinate) {
  for (auto profile : profiles_with_pq_hybrid_key_share()) {
    int valid_count = 0;
    for (td::uint64 seed = 0; seed < 256; seed++) {
      auto owned = build_with_pq(profile, seed);
      const auto *entry = find_pq_hybrid_entry(owned.hello);
      ASSERT_TRUE(entry != nullptr);
      ASSERT_EQ(static_cast<size_t>(kHybridTotalLength), entry->key_data.size());

      auto x25519_tail =
          entry->key_data.substr(entry->key_data.size() - kX25519KeyShareLength, kX25519KeyShareLength);
      if (is_valid_curve25519_public_coordinate(x25519_tail)) {
        valid_count++;
      }
    }
    ASSERT_EQ(256, valid_count);
  }
}

// C1 — The wire-format length field 0x04C0 must always be present
// somewhere in the raw wire byte stream after the PQ hybrid group code.
// We check this by exercising the parser, which decodes the length field
// and would fail with "PQ hybrid key_share length mismatch" for any other
// value. This is a positive regression-guard for the canonical length
// field byte sequence (`\x04\xC0`) appearing in the right wire position.
TEST(PqHybridKeyShareFormat, ParsedHybridLengthEqualsZero04c0Wire) {
  for (auto profile : profiles_with_pq_hybrid_key_share()) {
    auto owned = build_with_pq(profile, 0);
    const auto *entry = find_pq_hybrid_entry(owned.hello);
    ASSERT_TRUE(entry != nullptr);
    ASSERT_EQ(static_cast<td::uint16>(0x04C0), entry->key_length);
  }
}

// Defensive: profiles without PQ (Chrome 120 — predates Chrome PQ adoption,
// Android OkHttp advisory) MUST NOT have a PQ hybrid entry at all. The
// wire format invariant is the absence of an entry, not just the absence
// of the group from supported_groups.
TEST(PqHybridKeyShareFormat, NonPqProfilesDoNotEmitHybridKeyShareEntry) {
  for (auto profile : {BrowserProfile::Chrome120, BrowserProfile::Android11_OkHttp_Advisory}) {
    MockRng rng(0xCABA);
    auto wire =
        build_tls_client_hello_for_profile(kHost.str(), kSecret, kFixedUnixTime, profile, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    for (const auto &entry : parsed.ok().key_share_entries) {
      ASSERT_TRUE(entry.group != kPqHybridGroup);
    }
  }
}

}  // namespace
