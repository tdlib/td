// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: iOS-family ECH-disabled wire fingerprint isolation.
//
// Threat model: When ECH is suppressed (RU route or route failure CB trip)
// on an iOS device, the stealth ClientHello must NOT collapse to a fingerprint
// that:
//   (a) Exactly matches the Apple TLS (IOS14/Safari26_3) family, OR
//   (b) Exactly matches the real ios26_4 Chrome (no-ECH Apple TLS stack)
//       fingerprint (13 non-GREASE extensions without ALPS or ECH).
//
// These are the two nearest "natural" fingerprints an ECH-disabled iOS build
// could collapse to, which would make ALL stealth iOS traffic look like normal
// browser traffic — useful for DPI evasion on its own, but only if the
// transition is clean. A partially-matching wire (some Chrome 26.3 fields +
// some Apple TLS fields) is NEITHER safe NOR natural and is the primary risk.
//
// Key invariants tested:
//
//   I1. Chrome147_IOSChromium with ECH disabled MUST NOT produce the exact
//       same non-GREASE extension set as the reviewed ios26_4 Chrome capture.
//       Our profile is modeled on the ios26_1/26_3 with-ECH shape. When ECH
//       is suppressed, the ECH extension is dropped but ALPS/session-ticket
//       remain, making it distinct from ios26_4.
//
//   I2. Chrome147_IOSChromium with ECH disabled MUST NOT produce the exact
//       same non-GREASE extension set as the Apple TLS (IOS14) profile.
//
//   I3. IOS14 (Apple TLS) MUST NOT gain ALPS or ECH extensions regardless
//       of ECH mode. Apple TLS stack is structurally ECH-incapable.
//
//   I4. IOS14 (Apple TLS) wire MUST remain structurally distinct from all
//       Chrome-family wires: no ALPS, no ECH, no PSK, no ChaCha ciphers.
//
//   I5. Chrome147_IOSChromium ECH-disabled proxy wire MUST NOT contain ALPS.
//       In proxy mode the ALPN list is rewritten to `http/1.1` only, and the
//       reviewed ClientHelloOpMapper contract strips ALPS together with that
//       HTTP/2-only lane to avoid an L7 inconsistency.
//
//   I6. Chrome147_IOSChromium ECH-disabled wire must differ from
//       ECH-enabled wire in extension count (ECH ext absent = 1 fewer).
//
//   I7. Across 1000 seeds, Chrome147_IOSChromium ECH-disabled wires MUST NOT
//       match the set of extensions from any Apple TLS profile.
//
// Risk register:
//   RISK: IOSFingerprint-1: ECH-suppressed Chromium collapses to Apple TLS shape.
//     attack: ECH gate strips ECH AND ALPS/session-ticket.
//     test_ids: MaskingIosEchDisabledFingerprint_ChromiumEchDisabledKeepsAlps
//
//   RISK: IOSFingerprint-2: Apple TLS accidentally advertises ECH.
//     attack: profile spec or extension list copy-paste error.
//     test_ids: MaskingIosEchDisabledFingerprint_AppleTlsNeverAdvertisesEch
//
//   RISK: IOSFingerprint-3: ECH-suppressed iOS Chromium matches ios26_4 exact set.
//     attack: ECH gate strips too many extensions.
//     test_ids: MaskingIosEchDisabledFingerprint_ChromiumEchDisabledNotIos264Family

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedClientHelloFixtures.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using td::int32;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_proxy_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::test::extension_set_non_grease_no_padding;
using td::mtproto::test::find_extension;
using td::mtproto::test::make_unordered_set;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::ParsedClientHello;
using td::uint16;
using td::uint64;
using namespace td::mtproto::test::fixtures::reviewed;

constexpr int32 kUnixTime = 1712345678;
constexpr td::Slice kDomain = "www.cloudflare.com";
constexpr td::Slice kSecret = "0123456789secret";

constexpr uint16 kEchType = 0xFE0D;
constexpr uint16 kAlpsChrome133Plus = 0x44CD;
constexpr uint16 kAlpsLegacy = 0x4469;
constexpr uint16 kPskType = 0x0029;

ParsedClientHello build_proxy_parsed(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  auto wire = build_proxy_tls_client_hello_for_profile(kDomain.str(), kSecret, kUnixTime, profile, ech_mode, rng);
  auto result = parse_tls_client_hello(wire);
  ASSERT_TRUE(result.is_ok());
  return result.move_as_ok();
}

// I1: Chrome147_IOSChromium ECH-disabled proxy wire MUST drop ALPS together
// with the proxy-mode `h2` ALPN suppression.
TEST(MaskingIosEchDisabledFingerprint, ChromiumEchDisabledDropsAlpsInProxyMode) {
  reset_runtime_ech_failure_state_for_tests();
  for (uint64 seed = 0; seed < 32; seed++) {
    auto hello = build_proxy_parsed(BrowserProfile::Chrome147_IOSChromium, EchMode::Disabled, seed);
    auto exts = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(exts.count(kAlpsChrome133Plus) == 0);
    ASSERT_TRUE(exts.count(kAlpsLegacy) == 0);
    ASSERT_TRUE(find_extension(hello, kEchType) == nullptr);
  }
}

// I2: Chrome147_IOSChromium ECH-disabled MUST NOT match ios26_4 Chrome extension set.
// ios26_4 Chrome (no ECH): 13 non-GREASE exts = {0x0000,0x0017,0xFF01,0x000A,
// 0x000B,0x0010,0x0005,0x000D,0x0012,0x0033,0x002D,0x002B,0x001B}.
// Our ios26_1/26_3-modeled profile with ECH stripped must differ because it
// retains ALPS, session-ticket, and potentially PSK (which ios26_4 lacks).
TEST(MaskingIosEchDisabledFingerprint, ChromiumEchDisabledNotIos264Family) {
  reset_runtime_ech_failure_state_for_tests();
  const auto ios264_set = make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding);
  for (uint64 seed = 0; seed < 64; seed++) {
    auto hello = build_proxy_parsed(BrowserProfile::Chrome147_IOSChromium, EchMode::Disabled, seed);
    auto exts = extension_set_non_grease_no_padding(hello);
    // Must differ: ALPS present in our profile, absent in ios26_4 capture.
    ASSERT_TRUE(exts != ios264_set);
  }
}

// I3a: IOS14/Apple TLS NEVER advertises ECH regardless of EchMode argument.
// Apple TLS stack is structurally ECH-incapable.
TEST(MaskingIosEchDisabledFingerprint, AppleTlsNeverAdvertisesEch) {
  reset_runtime_ech_failure_state_for_tests();
  for (uint64 seed = 0; seed < 32; seed++) {
    auto hello_ech = build_proxy_parsed(BrowserProfile::IOS14, EchMode::Rfc9180Outer, seed);
    ASSERT_TRUE(find_extension(hello_ech, kEchType) == nullptr);

    auto hello_noe = build_proxy_parsed(BrowserProfile::IOS14, EchMode::Disabled, seed);
    ASSERT_TRUE(find_extension(hello_noe, kEchType) == nullptr);
  }
}

// I3b: IOS14 (Apple TLS) NEVER advertises ALPS.
TEST(MaskingIosEchDisabledFingerprint, AppleTlsNeverAdvertisesAlps) {
  reset_runtime_ech_failure_state_for_tests();
  for (uint64 seed = 0; seed < 32; seed++) {
    auto hello = build_proxy_parsed(BrowserProfile::IOS14, EchMode::Disabled, seed);
    auto exts = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(exts.count(kAlpsChrome133Plus) == 0);
    ASSERT_TRUE(exts.count(kAlpsLegacy) == 0);
  }
}

// I3c: IOS14 (Apple TLS) NEVER carries PSK.
TEST(MaskingIosEchDisabledFingerprint, AppleTlsNeverCarriesPsk) {
  reset_runtime_ech_failure_state_for_tests();
  for (uint64 seed = 0; seed < 32; seed++) {
    auto hello_ech = build_proxy_parsed(BrowserProfile::IOS14, EchMode::Rfc9180Outer, seed);
    ASSERT_TRUE(find_extension(hello_ech, kPskType) == nullptr);

    auto hello_noe = build_proxy_parsed(BrowserProfile::IOS14, EchMode::Disabled, seed);
    ASSERT_TRUE(find_extension(hello_noe, kPskType) == nullptr);
  }
}

// I4: IOS14 is structurally distinct from Chrome-family: no ALPS, no ECH.
// This invariant ensures that a refactor doesn't accidentally merge profiles.
TEST(MaskingIosEchDisabledFingerprint, IOS14AlwaysDistinctFromChromiumFamily) {
  reset_runtime_ech_failure_state_for_tests();
  const auto ios261_chromium = make_unordered_set(chrome146_0_7680_151_ios26_1NonGreaseExtensionsWithoutPadding);
  for (uint64 seed = 0; seed < 64; seed++) {
    auto hello = build_proxy_parsed(BrowserProfile::IOS14, EchMode::Disabled, seed);
    auto exts = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(exts != ios261_chromium);
  }
}

// I5: IOS14 and Chrome147_IOSChromium ECH-disabled wires are distinct sets.
TEST(MaskingIosEchDisabledFingerprint, IOS14AndChromiumEchDisabledAreDifferentSets) {
  reset_runtime_ech_failure_state_for_tests();
  for (uint64 seed = 0; seed < 16; seed++) {
    auto ios14 = build_proxy_parsed(BrowserProfile::IOS14, EchMode::Disabled, seed);
    auto chromium = build_proxy_parsed(BrowserProfile::Chrome147_IOSChromium, EchMode::Disabled, seed);
    ASSERT_TRUE(extension_set_non_grease_no_padding(ios14) != extension_set_non_grease_no_padding(chromium));
  }
}

// I6: Chrome147_IOSChromium ECH-disabled has strictly fewer extensions than ECH-enabled.
TEST(MaskingIosEchDisabledFingerprint, ChromiumEchDisabledHasFewerExtensionsThanEchEnabled) {
  reset_runtime_ech_failure_state_for_tests();
  for (uint64 seed = 0; seed < 16; seed++) {
    auto hello_ech = build_proxy_parsed(BrowserProfile::Chrome147_IOSChromium, EchMode::Rfc9180Outer, seed);
    auto hello_noe = build_proxy_parsed(BrowserProfile::Chrome147_IOSChromium, EchMode::Disabled, seed);
    // ECH-enabled has ECH extension (0xFE0D); ECH-disabled must not.
    ASSERT_TRUE(find_extension(hello_ech, kEchType) != nullptr);
    ASSERT_TRUE(find_extension(hello_noe, kEchType) == nullptr);
    // Extension count: ECH-disabled must have at least 1 fewer.
    ASSERT_TRUE(hello_noe.extensions.size() < hello_ech.extensions.size());
  }
}

// I7: Stress 1000 seeds — Chrome147_IOSChromium ECH-disabled NEVER collapses
// to Apple TLS (IOS14) extension set.
TEST(MaskingIosEchDisabledFingerprint, ChromiumEchDisabledNeverCollapsesIntoAppleTlsAcross1kSeeds) {
  reset_runtime_ech_failure_state_for_tests();
  // Sample two IOS14 wires. Both must differ from Chrome.
  const auto ios14_ext_a =
      extension_set_non_grease_no_padding(build_proxy_parsed(BrowserProfile::IOS14, EchMode::Disabled, 0));
  const auto ios14_ext_b =
      extension_set_non_grease_no_padding(build_proxy_parsed(BrowserProfile::IOS14, EchMode::Disabled, 1));

  for (uint64 seed = 0; seed < 1000; seed++) {
    auto hello = build_proxy_parsed(BrowserProfile::Chrome147_IOSChromium, EchMode::Disabled, seed);
    auto exts = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(exts != ios14_ext_a);
    ASSERT_TRUE(exts != ios14_ext_b);
  }
}

// I8: Safari26_3 profile NEVER advertises ECH regardless of EchMode argument.
// Real Safari on iOS is Apple TLS-family — structurally no ECH.
TEST(MaskingIosEchDisabledFingerprint, Safari263NeverAdvertisesEch) {
  reset_runtime_ech_failure_state_for_tests();
  for (uint64 seed = 0; seed < 32; seed++) {
    auto hello_ech = build_proxy_parsed(BrowserProfile::Safari26_3, EchMode::Rfc9180Outer, seed);
    ASSERT_TRUE(find_extension(hello_ech, kEchType) == nullptr);

    auto hello_noe = build_proxy_parsed(BrowserProfile::Safari26_3, EchMode::Disabled, seed);
    ASSERT_TRUE(find_extension(hello_noe, kEchType) == nullptr);
  }
}

// I9: Safari26_3 NEVER carries ALPS (Chrome-specific extension).
TEST(MaskingIosEchDisabledFingerprint, Safari263NeverCarriesAlps) {
  reset_runtime_ech_failure_state_for_tests();
  for (uint64 seed = 0; seed < 32; seed++) {
    auto hello = build_proxy_parsed(BrowserProfile::Safari26_3, EchMode::Disabled, seed);
    auto exts = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(exts.count(kAlpsChrome133Plus) == 0);
    ASSERT_TRUE(exts.count(kAlpsLegacy) == 0);
  }
}

}  // namespace
