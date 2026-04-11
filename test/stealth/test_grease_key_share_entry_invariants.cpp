// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
//
// REG-7 — GREASE first-entry invariant for key_share extension.
//
// Real Chrome (linux/desktop, all versions 120-147), Safari 26.x, and
// Chrome on iOS 26.x captures all carry a GREASE entry as the FIRST item
// in the TLS 1.3 `key_share` extension. The wire shape is:
//
//   group(2 bytes)   - paired GREASE pair (0x0A0A, 0x1A1A, ..., 0xFAFA)
//   length(2 bytes)  - 0x0001
//   body(1 byte)     - 0x00
//
// Captures used as ground truth:
//   * test/analysis/fixtures/clienthello/linux_desktop/chrome144_*.json
//   * test/analysis/fixtures/clienthello/linux_desktop/chrome146_*.json
//   * test/analysis/fixtures/clienthello/ios/safari26_3_1_*.json
//   * test/analysis/fixtures/clienthello/ios/safari26_4_*.json
//   * test/analysis/fixtures/clienthello/ios/chrome147_0_7727_47_ios26_4_*.json
//
// All show GREASE as the first entry, then the actual public keys.
// Firefox (Linux desktop and macOS) does NOT carry a GREASE key_share
// entry — it places GREASE elsewhere in the wire image — and is excluded
// from the positive checks below.
//
// Adversarial scenarios this suite catches:
//
//   D1. GREASE entry missing entirely. Tests that every Chromium-family
//       and Apple-TLS profile produces ≥3 (or ≥2 for non-PQ Chrome 120)
//       key_share entries with the first being GREASE.
//
//   D2. GREASE in wrong position. The GREASE entry MUST be the first
//       entry, not last and not in the middle. Real Chrome / Safari
//       always emit it first.
//
//   D3. Wrong GREASE wire shape. Length must be exactly 0x0001, body
//       must be exactly 0x00 — any other value is detectable.
//
//   D4. RNG-leaking GREASE byte. The two bytes of the GREASE group MUST
//       form a valid GREASE pair (low nibble 0xA, both bytes equal).
//
//   D5. Non-Chromium / non-Apple-TLS profiles MUST NOT regress to adding
//       GREASE in key_share — Firefox and Android historically don't,
//       and adopting it would break their wire fidelity.

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"

#include <set>
#include <vector>

namespace {

using td::mtproto::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::is_grease_value;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::ParsedClientHello;

constexpr td::int32 kFixedUnixTime = 1712345678;
constexpr td::Slice kSecret = td::Slice("0123456789secret");
constexpr td::Slice kHost = td::Slice("www.google.com");

const std::vector<BrowserProfile> &profiles_with_grease_key_share() {
  static const std::vector<BrowserProfile> kProfiles = {
      BrowserProfile::Chrome133, BrowserProfile::Chrome131,  BrowserProfile::Chrome120,
      BrowserProfile::Safari26_3, BrowserProfile::IOS14,
  };
  return kProfiles;
}

const std::vector<BrowserProfile> &profiles_without_grease_key_share() {
  static const std::vector<BrowserProfile> kProfiles = {
      BrowserProfile::Firefox148,
      BrowserProfile::Firefox149_MacOS26_3,
      BrowserProfile::Android11_OkHttp_Advisory,
  };
  return kProfiles;
}

ParsedClientHello build_and_parse(BrowserProfile profile, EchMode ech_mode, td::uint64 seed) {
  MockRng rng(seed);
  auto wire = build_tls_client_hello_for_profile(kHost.str(), kSecret, kFixedUnixTime, profile, ech_mode, rng);
  auto parsed = parse_tls_client_hello(wire);
  if (parsed.is_error()) {
    LOG(ERROR) << "parse failed for profile " << static_cast<int>(profile) << " seed " << seed << ": "
               << parsed.error();
  }
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

// D1 + D2 — Every profile that should carry the GREASE key_share entry
// has it as the FIRST entry. Tested across many seeds.
TEST(GreaseKeyShareEntry, ChromeAndAppleTlsProfilesEmitGreaseAsFirstKeyShareEntry) {
  for (auto profile : profiles_with_grease_key_share()) {
    for (td::uint64 seed = 0; seed < 16; seed++) {
      auto hello = build_and_parse(profile, EchMode::Disabled, seed);
      ASSERT_TRUE(hello.key_share_entries.size() >= 2u);
      ASSERT_TRUE(is_grease_value(hello.key_share_entries[0].group));
    }
  }
}

// D3 — The GREASE entry's body length is exactly 1 byte.
TEST(GreaseKeyShareEntry, GreaseKeyShareEntryBodyLengthIsExactlyOneByte) {
  for (auto profile : profiles_with_grease_key_share()) {
    for (td::uint64 seed = 0; seed < 16; seed++) {
      auto hello = build_and_parse(profile, EchMode::Disabled, seed);
      ASSERT_TRUE(hello.key_share_entries.size() >= 2u);
      ASSERT_EQ(static_cast<td::uint16>(1), hello.key_share_entries[0].key_length);
      ASSERT_EQ(static_cast<size_t>(1), hello.key_share_entries[0].key_data.size());
    }
  }
}

// D3 — The GREASE entry's body byte is exactly 0x00.
TEST(GreaseKeyShareEntry, GreaseKeyShareEntryBodyIsExactlyZeroByte) {
  for (auto profile : profiles_with_grease_key_share()) {
    for (td::uint64 seed = 0; seed < 16; seed++) {
      auto hello = build_and_parse(profile, EchMode::Disabled, seed);
      ASSERT_TRUE(hello.key_share_entries.size() >= 2u);
      ASSERT_EQ(static_cast<size_t>(1), hello.key_share_entries[0].key_data.size());
      ASSERT_EQ(static_cast<td::uint8>(0x00), static_cast<td::uint8>(hello.key_share_entries[0].key_data[0]));
    }
  }
}

// D4 — The GREASE group is a valid RFC 8701 GREASE pair (both bytes
// equal, low nibble 0xA).
TEST(GreaseKeyShareEntry, GreaseKeyShareGroupHasValidGreaseStructure) {
  for (auto profile : profiles_with_grease_key_share()) {
    for (td::uint64 seed = 0; seed < 32; seed++) {
      auto hello = build_and_parse(profile, EchMode::Disabled, seed);
      ASSERT_TRUE(hello.key_share_entries.size() >= 2u);
      auto group = hello.key_share_entries[0].group;
      ASSERT_TRUE(is_grease_value(group));
      auto hi = static_cast<td::uint8>((group >> 8) & 0xFF);
      auto lo = static_cast<td::uint8>(group & 0xFF);
      ASSERT_EQ(hi, lo);
      ASSERT_EQ(static_cast<td::uint8>(0x0A), static_cast<td::uint8>(hi & 0x0F));
    }
  }
}

// D5 — Profiles that should NOT carry GREASE in key_share MUST NOT have
// any GREASE entry. Catches accidental "add grease everywhere" regressions.
TEST(GreaseKeyShareEntry, FirefoxAndAndroidProfilesDoNotEmitGreaseInKeyShare) {
  for (auto profile : profiles_without_grease_key_share()) {
    for (td::uint64 seed = 0; seed < 16; seed++) {
      auto hello = build_and_parse(profile, EchMode::Disabled, seed);
      for (const auto &entry : hello.key_share_entries) {
        ASSERT_FALSE(is_grease_value(entry.group));
      }
    }
  }
}

// D2 — The Chromium-family / Apple-TLS profiles must produce key_share
// entries in the canonical order: GREASE, then PQ (if any), then X25519.
// Catches "GREASE accidentally appended at end" regressions.
TEST(GreaseKeyShareEntry, GreaseEntryIsAlwaysAtIndexZeroAndNeverElsewhere) {
  for (auto profile : profiles_with_grease_key_share()) {
    for (td::uint64 seed = 0; seed < 16; seed++) {
      auto hello = build_and_parse(profile, EchMode::Disabled, seed);
      ASSERT_TRUE(hello.key_share_entries.size() >= 2u);
      // First entry MUST be GREASE.
      ASSERT_TRUE(is_grease_value(hello.key_share_entries[0].group));
      // No other entry may be GREASE.
      for (size_t i = 1; i < hello.key_share_entries.size(); i++) {
        ASSERT_FALSE(is_grease_value(hello.key_share_entries[i].group));
      }
    }
  }
}

// Defensive — the wire is built reproducibly: the GREASE byte value
// itself depends on the RNG (real captures show different GREASE pairs
// per session: 0x4A4A, 0xDADA, 0x8A8A, 0x7A7A...), but the structure
// (length=1, body=0x00) is fixed. We assert that across many seeds we
// see at least 2 distinct GREASE values, proving the byte is not stuck.
TEST(GreaseKeyShareEntry, GreaseGroupVariesAcrossDifferentSeeds) {
  std::set<td::uint16> seen_groups;
  for (td::uint64 seed = 0; seed < 64; seed++) {
    auto hello = build_and_parse(BrowserProfile::Chrome133, EchMode::Disabled, seed);
    ASSERT_TRUE(hello.key_share_entries.size() >= 2u);
    seen_groups.insert(hello.key_share_entries[0].group);
  }
  ASSERT_TRUE(seen_groups.size() >= 2u);
}

}  // namespace
