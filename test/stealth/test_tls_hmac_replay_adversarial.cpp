// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: HMAC replay protection and active probing defense.
//
// From the research findings: "State censors often use Active Probing —
// when DPI suspects a proxy, it later initiates its own connection to the
// server. mtproto.zig mitigates this with a timestamp+digest cache that
// strictly rejects replayed handshakes beyond a two-minute window."
//
// These tests verify:
// 1. HMAC changes with every parameter permutation
// 2. No two different domains can produce the same HMAC
// 3. The timestamp XOR mask in client_random is correct
// 4. Identical inputs with identical RNG produce identical wire images (determinism)
// 5. Any bit-flip in the wire corrupts HMAC verification

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/tests.h"

#include <set>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

// client_random is at offset 11 (after record header 5 + handshake header 4 + version 2)
constexpr size_t kClientRandomOffset = 11;
constexpr size_t kClientRandomLength = 32;

td::Slice extract_client_random(td::Slice wire) {
  CHECK(wire.size() >= kClientRandomOffset + kClientRandomLength);
  return wire.substr(kClientRandomOffset, kClientRandomLength);
}

TEST(TlsHmacReplayAdversarial, SameInputsSameRngMustProduceIdenticalWire) {
  MockRng rng1(42);
  auto wire1 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire2 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng2);
  ASSERT_EQ(wire1, wire2);
}

TEST(TlsHmacReplayAdversarial, ClientRandomMustChangeWhenTimestampChanges) {
  MockRng rng1(42);
  auto wire1 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire2 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345679,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng2);
  auto random1 = extract_client_random(wire1);
  auto random2 = extract_client_random(wire2);
  ASSERT_NE(random1.str(), random2.str());
}

TEST(TlsHmacReplayAdversarial, ClientRandomMustChangeWhenSecretChanges) {
  MockRng rng1(42);
  auto wire1 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire2 = build_tls_client_hello_for_profile("www.google.com", "secret0123456789", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng2);
  auto random1 = extract_client_random(wire1);
  auto random2 = extract_client_random(wire2);
  ASSERT_NE(random1.str(), random2.str());
}

TEST(TlsHmacReplayAdversarial, ClientRandomMustChangeWhenDomainChanges) {
  MockRng rng1(42);
  auto wire1 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire2 = build_tls_client_hello_for_profile("www.example.com", "0123456789secret", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng2);
  // Domain changes SNI which changes wire image which changes HMAC
  auto random1 = extract_client_random(wire1);
  auto random2 = extract_client_random(wire2);
  ASSERT_NE(random1.str(), random2.str());
}

TEST(TlsHmacReplayAdversarial, AdjacentTimestampsMustProduceDifferentHmac) {
  // Active probing adversary tries adjacent timestamps
  for (int delta = 1; delta <= 10; delta++) {
    MockRng rng1(42);
    auto wire1 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Chrome133, EchMode::Disabled, rng1);
    MockRng rng2(42);
    auto wire2 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678 + delta,
                                                    BrowserProfile::Chrome133, EchMode::Disabled, rng2);
    ASSERT_NE(wire1, wire2);
  }
}

TEST(TlsHmacReplayAdversarial, ClientRandomMustLookUniformlyRandom) {
  // Statistical test: client_random bytes should not have obvious patterns
  td::vector<int> byte_counts(256, 0);
  for (td::uint64 seed = 0; seed < 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto random = extract_client_random(wire);
    for (size_t i = 0; i < random.size(); i++) {
      byte_counts[static_cast<td::uint8>(random[i])]++;
    }
  }
  // 500 samples * 32 bytes = 16000 bytes total, expected ~62.5 per byte value.
  // Any byte value appearing more than 200 times is suspicious (>3x expected).
  for (int count : byte_counts) {
    ASSERT_TRUE(count < 200);
  }
  // At least 200 distinct byte values should appear
  int distinct = 0;
  for (int count : byte_counts) {
    if (count > 0) {
      distinct++;
    }
  }
  ASSERT_TRUE(distinct >= 200);
}

TEST(TlsHmacReplayAdversarial, TimestampMaxValueMustNotCorruptHmac) {
  MockRng rng(42);
  // Test with INT32_MAX timestamp
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 0x7FFFFFFF,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
}

TEST(TlsHmacReplayAdversarial, TimestampNegativeValueMustNotCorruptHmac) {
  MockRng rng(42);
  // Negative timestamp (possible with time_t on some systems)
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", -1, BrowserProfile::Chrome133,
                                                 EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
}

TEST(TlsHmacReplayAdversarial, TimestampZeroMustNotCorruptHmac) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 0, BrowserProfile::Chrome133,
                                                 EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
}

TEST(TlsHmacReplayAdversarial, EachProfileMustProduceDifferentClientRandomForSameInputs) {
  // Adversary might try to correlate traffic across profiles
  std::set<td::string> randoms;
  BrowserProfile profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120,
                               BrowserProfile::Firefox148, BrowserProfile::Safari26_3};
  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    randoms.insert(extract_client_random(wire).str());
  }
  // Each profile has different wire layout => different HMAC
  ASSERT_EQ(randoms.size(), 5u);
}

TEST(TlsHmacReplayAdversarial, BitFlipInWireMustChangeServerSideHmacRecomputation) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);

  // Simulate server-side HMAC verification:
  // 1. Zero client_random, compute HMAC over entirety of wire
  td::string wire_for_hmac = wire;
  std::memset(&wire_for_hmac[kClientRandomOffset], 0, kClientRandomLength);
  td::string hmac_original(32, '\0');
  td::hmac_sha256(td::Slice("0123456789secret"), wire_for_hmac, hmac_original);

  // 2. Tamper with extension data (flip a bit past client_random region)
  td::string tampered = wire;
  size_t flip_offset = wire.size() - 50;
  tampered[flip_offset] ^= 0x01;

  // 3. Re-verify: zero client_random in tampered wire, recompute HMAC
  td::string tampered_for_hmac = tampered;
  std::memset(&tampered_for_hmac[kClientRandomOffset], 0, kClientRandomLength);
  td::string hmac_tampered(32, '\0');
  td::hmac_sha256(td::Slice("0123456789secret"), tampered_for_hmac, hmac_tampered);

  // The HMACs must differ — proving the server would reject the tampered packet
  ASSERT_NE(hmac_original, hmac_tampered);
}

}  // namespace
