// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
//
// REG-4 — ECH HPKE encapsulated key X25519 coordinate validity invariant.
//
// The TLS Encrypted ClientHello (ECH) extension for HPKE-X25519 carries a
// 32-byte encapsulated key field that MUST be a valid X25519 public point
// (the x-coordinate of a Curve25519 point). The y-coordinate must be a
// quadratic residue mod p25519, and the canonical Chrome implementation
// rejects any client that emits raw random bytes here because:
//
//   1. Cloudflare / Google ECH-aware servers strictly validate the field
//      against the X25519 curve equation y^2 = x^3 + 486662*x^2 + x mod p.
//      Random 32 bytes pass this check with probability ≈ 1/2, so half of
//      all our ECH-enabled connections would be rejected at the server,
//      and the failure rate would itself be a fingerprint.
//
//   2. A DPI box that ships its own X25519 validator (FreeBSD libsodium,
//      OpenSSL EVP_PKEY_X25519) can flag clients that produce invalid
//      coordinates as "synthetic / non-browser" with high precision.
//
//   3. Even without explicit validation, the ~50% server-side rejection
//      rate that raw-random `enc` produces is itself a strong behavioural
//      fingerprint at the ECH-aware ASN level.
//
// The historical (pre-c7f013608) `TlsHelloBuilder.cpp` solved this by
// reusing the same X25519 rejection-sampling helper used for the standard
// key_share extension. The c7f013608 refactor moved hello generation into
// a declarative `ClientHelloOpMapper` + `ClientHelloExecutor` pipeline but
// the ECH path was rewritten to emit `Op::random_bytes(32)` instead of
// re-using the X25519 helper. That regression silently leaks invalid
// coordinates onto the wire on every ECH-enabled connection.
//
// This test suite enforces the X25519 coordinate validity invariant on
// the ECH `enc` field for every profile that emits ECH (Chrome 120/131/133,
// Firefox 148/149_macOS) across many seeds. The check is the same one the
// existing `parse_tls_client_hello` parser already runs (the parser owns
// the curve-residue math, we just exercise it through real builder runs).
//
// Black-hat scenarios this guard catches:
//
//   B1. Direct regression to `Op::random_bytes(32)` for ECH `enc`.
//   B2. Wrong key length (e.g. 31 or 33 bytes) breaking the X25519 wire
//       contract.
//   B3. RNG-state-dependent validity (e.g. some seeds produce valid
//       coordinates by accident, hiding the bug behind a non-uniform RNG).
//   B4. Length field mismatch — the 2-byte length prefix in the ECH
//       wire format must match the actual key bytes.

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
using td::mtproto::test::find_extension;
using td::mtproto::test::is_valid_curve25519_public_coordinate;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::ParsedClientHello;
using td::mtproto::test::fixtures::kEchExtensionType;
using td::mtproto::test::fixtures::kX25519KeyShareLength;

constexpr td::int32 kFixedUnixTime = 1712345678;
constexpr td::Slice kSecret = td::Slice("0123456789secret");
constexpr td::Slice kHost = td::Slice("www.google.com");

const std::vector<BrowserProfile> &profiles_with_ech() {
  static const std::vector<BrowserProfile> kProfiles = {
      BrowserProfile::Chrome133,
      BrowserProfile::Chrome131,
      BrowserProfile::Chrome120,
      BrowserProfile::Firefox148,
      BrowserProfile::Firefox149_MacOS26_3,
  };
  return kProfiles;
}

// `ParsedClientHello` carries non-owning `Slice`s into the wire buffer it
// was parsed from. The owning wrapper keeps the wire alive for the
// lifetime of the parse result so that slice access does not return paint
// bytes from freed heap on Debug MSVC builds.
struct OwnedParsedClientHello final {
  std::string wire;
  ParsedClientHello hello;
};

OwnedParsedClientHello build_with_ech(BrowserProfile profile, td::uint64 seed) {
  MockRng rng(seed);
  OwnedParsedClientHello result;
  result.wire = build_tls_client_hello_for_profile(kHost.str(), kSecret, kFixedUnixTime, profile,
                                                   EchMode::Rfc9180Outer, rng);
  auto parsed = parse_tls_client_hello(result.wire);
  if (parsed.is_error()) {
    LOG(ERROR) << "ECH parse failed for profile " << static_cast<int>(profile) << " seed " << seed << ": "
               << parsed.error();
  }
  CHECK(parsed.is_ok());
  result.hello = parsed.move_as_ok();
  return result;
}

// B1 — Every profile with ECH must emit a 32-byte enc that is a valid
// Curve25519 x-coordinate. Run across many seeds to catch RNG-dependent
// regressions.
TEST(EchEncapsulatedKeyValidity, EveryEchProfileEmitsValidX25519Encapsulated) {
  for (auto profile : profiles_with_ech()) {
    for (td::uint64 seed = 0; seed < 32; seed++) {
      auto owned = build_with_ech(profile, seed);

      const auto *ech = find_extension(owned.hello, kEchExtensionType);
      ASSERT_TRUE(ech != nullptr);

      ASSERT_EQ(static_cast<td::uint16>(kX25519KeyShareLength), owned.hello.ech_actual_enc_length);
      ASSERT_EQ(static_cast<td::uint16>(kX25519KeyShareLength), owned.hello.ech_declared_enc_length);

      ASSERT_EQ(static_cast<size_t>(kX25519KeyShareLength), owned.hello.ech_enc.size());
      ASSERT_TRUE(is_valid_curve25519_public_coordinate(owned.hello.ech_enc));
    }
  }
}

// B3 — Statistical robustness against the regression scenario "use raw
// random bytes for the ECH encapsulated key". A uniformly random 32-byte
// sequence is a valid Curve25519 x-coordinate only ~50% of the time. The
// rejection-sampling helper `store_x25519_key_share()` guarantees 100%.
// This test asserts that a wide sample of seeds — 256 — produces 0 invalid
// `enc` keys; the probability of that outcome under the bug scenario is
// approximately (1/2)^256 ≈ 0.
TEST(EchEncapsulatedKeyValidity, AcrossWideSeedSweepEveryEchEncapsulatedKeyIsValidCoordinate) {
  for (auto profile : profiles_with_ech()) {
    int valid_count = 0;
    for (td::uint64 seed = 0; seed < 256; seed++) {
      auto owned = build_with_ech(profile, seed);
      ASSERT_EQ(static_cast<size_t>(kX25519KeyShareLength), owned.hello.ech_enc.size());
      if (is_valid_curve25519_public_coordinate(owned.hello.ech_enc)) {
        valid_count++;
      }
    }
    ASSERT_EQ(256, valid_count);
  }
}

// B4 — Wire length field consistency. The declared length on the wire
// must match the actual bytes consumed.
TEST(EchEncapsulatedKeyValidity, EchDeclaredLengthMatchesActualKeyLengthAcrossAllProfiles) {
  for (auto profile : profiles_with_ech()) {
    for (td::uint64 seed = 0; seed < 16; seed++) {
      auto owned = build_with_ech(profile, seed);
      ASSERT_EQ(owned.hello.ech_declared_enc_length, owned.hello.ech_actual_enc_length);
    }
  }
}

// Defensive — ECH-disabled profiles must NOT emit any ECH bytes at all.
TEST(EchEncapsulatedKeyValidity, EchDisabledProfilesDoNotEmitEchExtension) {
  for (auto profile : profiles_with_ech()) {
    MockRng rng(0xFEED);
    auto wire = build_tls_client_hello_for_profile(kHost.str(), kSecret, kFixedUnixTime, profile, EchMode::Disabled,
                                                   rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), kEchExtensionType) == nullptr);
  }
}

}  // namespace
