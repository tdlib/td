// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: Chrome and Firefox have structurally different ECH
// and extension configurations. A DPI that knows both signatures can
// detect mixing violations. These tests verify structural fidelity of
// each browser profile against its expected wire characteristics.

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::profile_spec;
using td::mtproto::test::find_extension;
using td::mtproto::test::fixtures::kAlpsChrome131;
using td::mtproto::test::fixtures::kAlpsChrome133Plus;
using td::mtproto::test::fixtures::kEchExtensionType;
using td::mtproto::test::fixtures::kPqHybridGroup;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

TEST(TlsProfileStructuralDifferential, Chrome133MustHaveAlps44CD) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  auto *alps = find_extension(parsed.ok(), kAlpsChrome133Plus);
  ASSERT_TRUE(alps != nullptr);
  // Chrome133 MUST NOT have ALPS 0x4469 (Chrome131 codepoint)
  auto *alps_131 = find_extension(parsed.ok(), kAlpsChrome131);
  ASSERT_TRUE(alps_131 == nullptr);
}

TEST(TlsProfileStructuralDifferential, Chrome131MustHaveAlps4469) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome131, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  auto *alps = find_extension(parsed.ok(), kAlpsChrome131);
  ASSERT_TRUE(alps != nullptr);
  // Chrome131 MUST NOT have ALPS 0x44CD
  auto *alps_133 = find_extension(parsed.ok(), kAlpsChrome133Plus);
  ASSERT_TRUE(alps_133 == nullptr);
}

TEST(TlsProfileStructuralDifferential, Chrome120MustNotHavePqKeyShare) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome120, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  // Chrome120 is non-PQ
  for (const auto &entry : parsed.ok().key_share_entries) {
    ASSERT_TRUE(entry.group != kPqHybridGroup);
  }
}

TEST(TlsProfileStructuralDifferential, Chrome133WithEchMustHaveEchExtension) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  auto *ech = find_extension(parsed.ok(), kEchExtensionType);
  ASSERT_TRUE(ech != nullptr);
  // Chrome uses AEAD 0x0001 (AES-128-GCM)
  ASSERT_EQ(static_cast<td::uint16>(0x0001), parsed.ok().ech_aead_id);
}

TEST(TlsProfileStructuralDifferential, Firefox148WithEchMustHaveCorrectAeadId) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  auto *ech = find_extension(parsed.ok(), kEchExtensionType);
  ASSERT_TRUE(ech != nullptr);
  // Firefox uses AEAD 0x0003 (ChaCha20-Poly1305)
  ASSERT_EQ(static_cast<td::uint16>(0x0003), parsed.ok().ech_aead_id);
}

TEST(TlsProfileStructuralDifferential, Firefox148MustNotHaveAlpsExtension) {
  // Firefox does not use ALPS.
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Firefox148, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  auto *alps_1 = find_extension(parsed.ok(), kAlpsChrome133Plus);
  auto *alps_2 = find_extension(parsed.ok(), kAlpsChrome131);
  ASSERT_TRUE(alps_1 == nullptr);
  ASSERT_TRUE(alps_2 == nullptr);
}

TEST(TlsProfileStructuralDifferential, Firefox148MustHaveRecordSizeLimitExtension) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Firefox148, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  // record_size_limit extension type is 0x001C
  auto *rsl = find_extension(parsed.ok(), 0x001C);
  ASSERT_TRUE(rsl != nullptr);
}

TEST(TlsProfileStructuralDifferential, Firefox148CipherSuitesMustDifferFromChrome) {
  MockRng rng1(42);
  auto chrome_wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                        BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto firefox_wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                         BrowserProfile::Firefox148, EchMode::Disabled, rng2);

  auto chrome_parsed = parse_tls_client_hello(chrome_wire);
  auto firefox_parsed = parse_tls_client_hello(firefox_wire);
  ASSERT_TRUE(chrome_parsed.is_ok());
  ASSERT_TRUE(firefox_parsed.is_ok());

  // Cipher suites must differ (Firefox has a different ordering and set)
  ASSERT_TRUE(chrome_parsed.ok().cipher_suites != firefox_parsed.ok().cipher_suites);
}

TEST(TlsProfileStructuralDifferential, SafariMustNotHaveEchEvenWhenRequested) {
  // Safari profile doesn't support ECH.
  auto &spec = profile_spec(BrowserProfile::Safari26_3);
  ASSERT_FALSE(spec.allows_ech);

  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Safari26_3, EchMode::Rfc9180Outer, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  auto *ech = find_extension(parsed.ok(), kEchExtensionType);
  // ECH must be absent because the profile doesn't allow it.
  ASSERT_TRUE(ech == nullptr);
}

TEST(TlsProfileStructuralDifferential, Chrome133PqKeyShareMustHaveCorrectGroupAndLength) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  bool found_pq = false;
  bool found_x25519 = false;
  for (const auto &entry : parsed.ok().key_share_entries) {
    if (entry.group == kPqHybridGroup) {
      found_pq = true;
      // ML-KEM-768 (1184) + X25519 (32) = 1216 = 0x04C0
      ASSERT_EQ(static_cast<td::uint16>(0x04C0), entry.key_length);
    }
    if (entry.group == 0x001D) {
      found_x25519 = true;
      ASSERT_EQ(static_cast<td::uint16>(32), entry.key_length);
    }
  }
  ASSERT_TRUE(found_pq);
  ASSERT_TRUE(found_x25519);
}

TEST(TlsProfileStructuralDifferential, Firefox148PqKeyShareMustHaveCorrectGroupAndLength) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Firefox148, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  bool found_pq = false;
  for (const auto &entry : parsed.ok().key_share_entries) {
    if (entry.group == kPqHybridGroup) {
      found_pq = true;
      ASSERT_EQ(static_cast<td::uint16>(0x04C0), entry.key_length);
    }
  }
  ASSERT_TRUE(found_pq);
}

}  // namespace
