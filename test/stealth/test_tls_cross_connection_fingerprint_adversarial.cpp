// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: A sophisticated DPI correlating multiple connections from
// the same source IP to detect synthetic TLS patterns. These tests simulate
// a black-hat observer collecting multiple ClientHello samples and checking
// for invariants that real browsers do not exhibit:
// 1. Session IDs that are always the same (should be random per connection).
// 2. Random bytes at offset 11..42 that correlate across connections.
// 3. Extension bodies that are byte-identical across connections.
// 4. Key share material that repeats.

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

TEST(TlsCrossConnectionFingerprintAdversarial, SessionIdsMustBeUniqueAcrossConnections) {
  std::unordered_set<td::string> session_ids;
  for (td::uint64 seed = 1; seed <= 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    session_ids.insert(parsed.ok().session_id.str());
  }
  // Every connection must have a unique session ID.
  ASSERT_EQ(200u, session_ids.size());
}

TEST(TlsCrossConnectionFingerprintAdversarial, X25519KeySharesMustBeUniqueAcrossConnections) {
  std::unordered_set<td::string> keys;
  for (td::uint64 seed = 1; seed <= 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto &hello = parsed.ok_ref();
    for (const auto &entry : hello.key_share_entries) {
      if (entry.group == 0x001D) {
        keys.insert(entry.key_data.str());
      }
    }
  }
  ASSERT_EQ(200u, keys.size());
}

TEST(TlsCrossConnectionFingerprintAdversarial, ClientRandomBytesMustBeUniqueAcrossConnections) {
  // bytes [11..42] contain client random (zeroed here for HMAC, but
  // verify the HMAC output is diverse across connections).
  std::unordered_set<td::string> randoms;
  for (td::uint64 seed = 1; seed <= 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    ASSERT_TRUE(wire.size() >= 43u);
    // Client random is at offset 11, 32 bytes
    randoms.insert(wire.substr(11, 32));
  }
  // HMAC depends on the full wire which varies per seed.
  ASSERT_EQ(200u, randoms.size());
}

TEST(TlsCrossConnectionFingerprintAdversarial, EchEncapsulatedKeyMustBeUniquePerConnection) {
  std::unordered_set<td::string> ech_keys;
  for (td::uint64 seed = 1; seed <= 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto &hello = parsed.ok_ref();
    if (!hello.ech_enc.empty()) {
      ech_keys.insert(hello.ech_enc.str());
    }
  }
  ASSERT_EQ(200u, ech_keys.size());
}

TEST(TlsCrossConnectionFingerprintAdversarial, FullWireImageMustBeUniquePerConnection) {
  // Even with the same domain, secret, and timestamp, different RNG seeds
  // must produce completely different wire images.
  std::unordered_set<td::string> wires;
  for (td::uint64 seed = 1; seed <= 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    wires.insert(wire);
  }
  ASSERT_EQ(200u, wires.size());
}

TEST(TlsCrossConnectionFingerprintAdversarial, SameTimestampDifferentDomainsMustDiffer) {
  // A DPI monitoring multiple domains from the same client.
  td::vector<td::string> domains = {"www.google.com", "www.facebook.com", "www.amazon.com", "www.cloudflare.com",
                                    "www.microsoft.com"};
  std::unordered_set<td::string> wires;
  for (const auto &domain : domains) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile(domain, "0123456789secret", 1712345678, BrowserProfile::Chrome133,
                                                   EchMode::Disabled, rng);
    wires.insert(wire);
  }
  // Different domains must produce different wire images (SNI changes the content).
  ASSERT_EQ(5u, wires.size());
}

TEST(TlsCrossConnectionFingerprintAdversarial, DifferentTimestampsMustProduceDifferentHmac) {
  // The HMAC at offset 11 embeds the timestamp. Different times = different HMAC.
  std::unordered_set<td::string> hmacs;
  for (td::int32 t = 1712345678; t < 1712345678 + 100; t++) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", t, BrowserProfile::Chrome133,
                                                   EchMode::Disabled, rng);
    ASSERT_TRUE(wire.size() >= 43u);
    hmacs.insert(wire.substr(11, 32));
  }
  // Each timestamp should produce a unique HMAC output.
  ASSERT_EQ(100u, hmacs.size());
}

}  // namespace
