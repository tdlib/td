// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: Cipher suite ordering determinism.
//
// From the research findings: Chrome and Firefox NEVER randomize cipher
// suite order. The cipher suite list is determined by BoringSSL/NSS
// compile-time priority. Any variation in cipher order across connections
// is an immediate DPI red flag, as it contradicts real browser behavior.
// JA4 segment B would not catch this (because it sorts), but a raw
// JA3 or positional analysis would.

#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::extract_cipher_suites;
using td::mtproto::test::is_grease_value;
using td::mtproto::test::join_uint16_decimal;
using td::mtproto::test::MockRng;

td::vector<td::uint16> extract_non_grease_cipher_suites(td::Slice wire) {
  auto all = extract_cipher_suites(wire);
  td::vector<td::uint16> result;
  for (auto cs : all) {
    if (!is_grease_value(cs)) {
      result.push_back(cs);
    }
  }
  return result;
}

TEST(TlsCipherSuiteOrderDeterminism, Chrome133CipherOrderMustBeIdenticalAcross500Seeds) {
  td::string reference;
  for (td::uint64 seed = 0; seed < 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto non_grease = extract_non_grease_cipher_suites(wire);
    auto joined = join_uint16_decimal(non_grease);
    if (seed == 0) {
      reference = joined;
      ASSERT_FALSE(non_grease.empty());
    } else {
      ASSERT_EQ(reference, joined);
    }
  }
}

TEST(TlsCipherSuiteOrderDeterminism, Chrome131CipherOrderMustBeIdenticalAcross500Seeds) {
  td::string reference;
  for (td::uint64 seed = 0; seed < 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome131, EchMode::Disabled, rng);
    auto non_grease = extract_non_grease_cipher_suites(wire);
    auto joined = join_uint16_decimal(non_grease);
    if (seed == 0) {
      reference = joined;
    } else {
      ASSERT_EQ(reference, joined);
    }
  }
}

TEST(TlsCipherSuiteOrderDeterminism, Chrome120CipherOrderMustBeIdenticalAcross500Seeds) {
  td::string reference;
  for (td::uint64 seed = 0; seed < 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome120, EchMode::Disabled, rng);
    auto non_grease = extract_non_grease_cipher_suites(wire);
    auto joined = join_uint16_decimal(non_grease);
    if (seed == 0) {
      reference = joined;
    } else {
      ASSERT_EQ(reference, joined);
    }
  }
}

TEST(TlsCipherSuiteOrderDeterminism, Firefox148CipherOrderMustBeIdenticalAcross500Seeds) {
  td::string reference;
  for (td::uint64 seed = 0; seed < 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Firefox148, EchMode::Disabled, rng);
    auto non_grease = extract_non_grease_cipher_suites(wire);
    auto joined = join_uint16_decimal(non_grease);
    if (seed == 0) {
      reference = joined;
    } else {
      ASSERT_EQ(reference, joined);
    }
  }
}

TEST(TlsCipherSuiteOrderDeterminism, SafariCipherOrderMustBeIdenticalAcross500Seeds) {
  td::string reference;
  for (td::uint64 seed = 0; seed < 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Safari26_3, EchMode::Disabled, rng);
    auto non_grease = extract_non_grease_cipher_suites(wire);
    auto joined = join_uint16_decimal(non_grease);
    if (seed == 0) {
      reference = joined;
    } else {
      ASSERT_EQ(reference, joined);
    }
  }
}

TEST(TlsCipherSuiteOrderDeterminism, Chrome133And131MustShareIdenticalCipherOrder) {
  MockRng rng1(42);
  auto wire131 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Chrome131, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire133 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Chrome133, EchMode::Disabled, rng2);
  // Chrome131 and Chrome133 use the SAME BoringSSL cipher suite list.
  // Only ALPS type differs, not the cipher priority.
  ASSERT_EQ(join_uint16_decimal(extract_non_grease_cipher_suites(wire131)),
            join_uint16_decimal(extract_non_grease_cipher_suites(wire133)));
}

TEST(TlsCipherSuiteOrderDeterminism, Firefox148CipherOrderMustDifferFromChrome) {
  MockRng rng1(42);
  auto wire_chrome = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                        BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire_firefox = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                         BrowserProfile::Firefox148, EchMode::Disabled, rng2);
  // Firefox (NSS) has a different cipher suite set and order than Chrome (BoringSSL).
  // A DPI matching against both libraries simultaneously would see distinct lists.
  ASSERT_NE(join_uint16_decimal(extract_non_grease_cipher_suites(wire_chrome)),
            join_uint16_decimal(extract_non_grease_cipher_suites(wire_firefox)));
}

TEST(TlsCipherSuiteOrderDeterminism, GreaseAlwaysFirstInChromeProfiles) {
  BrowserProfile chrome_profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120};
  for (auto profile : chrome_profiles) {
    for (td::uint64 seed = 0; seed < 50; seed++) {
      MockRng rng(seed);
      auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                     EchMode::Disabled, rng);
      auto all = extract_cipher_suites(wire);
      ASSERT_FALSE(all.empty());
      // Chrome places GREASE at position 0 in cipher suites
      ASSERT_TRUE(is_grease_value(all[0]));
      // No other non-first cipher should be GREASE
      for (size_t i = 1; i < all.size(); i++) {
        ASSERT_FALSE(is_grease_value(all[i]));
      }
    }
  }
}

TEST(TlsCipherSuiteOrderDeterminism, CipherSuiteCountMustMatchExpectedPerProfile) {
  struct ProfileExpectation {
    BrowserProfile profile;
    size_t expected_non_grease_count;
  };
  // Chrome profiles: 15 non-GREASE ciphers (3 TLS1.3 + 12 legacy)
  // Firefox: 17 non-GREASE ciphers (3 TLS1.3 + 14 legacy) — different set
  // Safari: 20 non-GREASE ciphers
  ProfileExpectation expectations[] = {
      {BrowserProfile::Chrome133, 15},
      {BrowserProfile::Chrome131, 15},
      {BrowserProfile::Chrome120, 15},
  };
  for (const auto &exp : expectations) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, exp.profile,
                                                   EchMode::Disabled, rng);
    auto non_grease = extract_non_grease_cipher_suites(wire);
    ASSERT_EQ(exp.expected_non_grease_count, non_grease.size());
  }
}

}  // namespace
