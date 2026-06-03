// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Contract tests: GREASE slot validity in generated wire.
//
// RFC 8701 defines GREASE values as 0x?A?A (where both bytes are equal
// and the low nibble is 0xA). A valid ClientHello must carry GREASE
// values that conform to this pattern at the reviewed structural
// positions. These tests verify:
//
//   1. Every GREASE value in the generated wire matches 0x_A_A.
//   2. GREASE slots appear at the Chrome-reviewed positions (cipher
//      suites[0], extensions[0], extensions[last], supported_groups[0],
//      supported_versions[0]).
//   3. Removing a GREASE slot from a reviewed position is detected.
//   4. Injecting a non-0x_A_A value (invalid GREASE) is detected by
//      the is_grease_value predicate.
//   5. Raw extension body lengths match the expected wire encoding.
//   6. Normalizing away GREASE (as JA3 does) must not hide GREASE
//      position drift -- the non-GREASE spine is invariant to GREASE
//      value changes but is NOT invariant to GREASE insertion/removal.

#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::extract_cipher_suites;
using td::mtproto::test::find_extension;
using td::mtproto::test::is_grease_value;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_cipher_suite_vector;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::ParsedClientHello;

constexpr td::int32 kFixedUnixTime = 1712345678;
constexpr const char *kHost = "www.google.com";
constexpr const char *kSecret = "0123456789secret";

ParsedClientHello build_and_parse(BrowserProfile profile, td::uint64 seed) {
  MockRng rng(seed);
  auto wire = build_tls_client_hello_for_profile(kHost, kSecret, kFixedUnixTime, profile, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

// ---------------------------------------------------------------------------
// 1. Every GREASE value across all structural locations conforms to 0x_A_A.
// ---------------------------------------------------------------------------
TEST(TlsGreaseSlotValidityContract, AllGreaseValuesMatchRfc8701Pattern) {
  BrowserProfile profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120,
                               BrowserProfile::Safari26_3, BrowserProfile::IOS14};
  for (auto profile : profiles) {
    for (td::uint64 seed = 0; seed < 64; seed++) {
      auto hello = build_and_parse(profile, seed);

      // Cipher suites
      auto ciphers = parse_cipher_suite_vector(hello.cipher_suites);
      ASSERT_TRUE(ciphers.is_ok());
      for (auto cs : ciphers.ok()) {
        if (is_grease_value(cs)) {
          auto hi = static_cast<td::uint8>((cs >> 8) & 0xFF);
          auto lo = static_cast<td::uint8>(cs & 0xFF);
          ASSERT_EQ(hi, lo);
          ASSERT_EQ(static_cast<td::uint8>(0x0A), static_cast<td::uint8>(hi & 0x0F));
        }
      }

      // Extension types
      for (const auto &ext : hello.extensions) {
        if (is_grease_value(ext.type)) {
          auto hi = static_cast<td::uint8>((ext.type >> 8) & 0xFF);
          auto lo = static_cast<td::uint8>(ext.type & 0xFF);
          ASSERT_EQ(hi, lo);
          ASSERT_EQ(static_cast<td::uint8>(0x0A), static_cast<td::uint8>(hi & 0x0F));
        }
      }

      // Supported groups
      for (auto group : hello.supported_groups) {
        if (is_grease_value(group)) {
          auto hi = static_cast<td::uint8>((group >> 8) & 0xFF);
          auto lo = static_cast<td::uint8>(group & 0xFF);
          ASSERT_EQ(hi, lo);
          ASSERT_EQ(static_cast<td::uint8>(0x0A), static_cast<td::uint8>(hi & 0x0F));
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 2. Chrome profiles have GREASE at all five reviewed positions.
// ---------------------------------------------------------------------------
TEST(TlsGreaseSlotValidityContract, ChromeGreaseSlotsAtReviewedPositions) {
  BrowserProfile chrome_profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120};
  for (auto profile : chrome_profiles) {
    for (td::uint64 seed = 0; seed < 50; seed++) {
      auto hello = build_and_parse(profile, seed);

      // Position 1: cipher_suites[0]
      auto ciphers = parse_cipher_suite_vector(hello.cipher_suites);
      ASSERT_TRUE(ciphers.is_ok());
      ASSERT_FALSE(ciphers.ok().empty());
      ASSERT_TRUE(is_grease_value(ciphers.ok()[0]));

      // Position 2: extensions[0]
      ASSERT_FALSE(hello.extensions.empty());
      ASSERT_TRUE(is_grease_value(hello.extensions[0].type));

      // Position 3: last non-padding extension is GREASE
      size_t last_idx = hello.extensions.size() - 1;
      if (hello.extensions[last_idx].type == 0x0015) {
        ASSERT_TRUE(last_idx > 0);
        last_idx--;
      }
      ASSERT_TRUE(is_grease_value(hello.extensions[last_idx].type));

      // Position 4: supported_groups[0]
      ASSERT_FALSE(hello.supported_groups.empty());
      ASSERT_TRUE(is_grease_value(hello.supported_groups[0]));

      // Position 5: supported_versions first entry is GREASE
      auto *sv_ext = find_extension(hello, 0x002B);
      ASSERT_TRUE(sv_ext != nullptr);
      ASSERT_TRUE(sv_ext->value.size() >= 3u);
      auto sv_hi = static_cast<td::uint8>(sv_ext->value[1]);
      auto sv_lo = static_cast<td::uint8>(sv_ext->value[2]);
      auto first_version = static_cast<td::uint16>((sv_hi << 8) | sv_lo);
      ASSERT_TRUE(is_grease_value(first_version));
    }
  }
}

// ---------------------------------------------------------------------------
// 3. Removing a GREASE slot from cipher_suites[0] produces a detectable
//    structural difference: the first cipher suite is no longer GREASE.
// ---------------------------------------------------------------------------
TEST(TlsGreaseSlotValidityContract, RemovedGreaseSlotInCipherSuitesDetected) {
  // Build a valid ClientHello and verify GREASE is present.
  auto hello = build_and_parse(BrowserProfile::Chrome133, 42);
  auto ciphers = parse_cipher_suite_vector(hello.cipher_suites);
  ASSERT_TRUE(ciphers.is_ok());
  ASSERT_TRUE(ciphers.ok().size() >= 2u);
  ASSERT_TRUE(is_grease_value(ciphers.ok()[0]));

  // Simulate removal: build a non-GREASE cipher list by stripping GREASE entries.
  td::vector<td::uint16> stripped;
  for (auto cs : ciphers.ok()) {
    if (!is_grease_value(cs)) {
      stripped.push_back(cs);
    }
  }
  // The stripped list must be smaller and its first entry must NOT be GREASE.
  ASSERT_TRUE(stripped.size() < ciphers.ok().size());
  ASSERT_FALSE(stripped.empty());
  ASSERT_FALSE(is_grease_value(stripped[0]));
}

// ---------------------------------------------------------------------------
// 4. Invalid GREASE values (not matching 0x_A_A) are correctly rejected
//    by the is_grease_value predicate.
// ---------------------------------------------------------------------------
TEST(TlsGreaseSlotValidityContract, InvalidGreaseValuesAreRejected) {
  // Valid GREASE values: 0x0A0A, 0x1A1A, ..., 0xFAFA (16 values total).
  td::uint16 valid_grease[] = {0x0A0A, 0x1A1A, 0x2A2A, 0x3A3A, 0x4A4A, 0x5A5A, 0x6A6A, 0x7A7A,
                               0x8A8A, 0x9A9A, 0xAAAA, 0xBABA, 0xCACA, 0xDADA, 0xEAEA, 0xFAFA};
  for (auto v : valid_grease) {
    ASSERT_TRUE(is_grease_value(v));
  }

  // Invalid: high byte != low byte
  ASSERT_FALSE(is_grease_value(0x0A1A));
  ASSERT_FALSE(is_grease_value(0x1A0A));
  ASSERT_FALSE(is_grease_value(0xFAEA));
  ASSERT_FALSE(is_grease_value(0xEAFA));

  // Invalid: low nibble is not 0xA
  ASSERT_FALSE(is_grease_value(0x0B0B));
  ASSERT_FALSE(is_grease_value(0x1010));
  ASSERT_FALSE(is_grease_value(0xFFFF));
  ASSERT_FALSE(is_grease_value(0x0000));
  ASSERT_FALSE(is_grease_value(0x0101));
  ASSERT_FALSE(is_grease_value(0xABAB));

  // Edge cases: real cipher suite values must not be detected as GREASE
  ASSERT_FALSE(is_grease_value(0x1301));  // TLS_AES_128_GCM_SHA256
  ASSERT_FALSE(is_grease_value(0x1302));  // TLS_AES_256_GCM_SHA384
  ASSERT_FALSE(is_grease_value(0xC02B));  // TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256
  ASSERT_FALSE(is_grease_value(0x001D));  // X25519 group
}

// ---------------------------------------------------------------------------
// 5. GREASE extension body length matches expected wire encoding (empty
//    body for type-only GREASE extensions, 1-byte body for key_share).
// ---------------------------------------------------------------------------
TEST(TlsGreaseSlotValidityContract, GreaseExtensionBodyLengthMatchesExpected) {
  for (td::uint64 seed = 0; seed < 50; seed++) {
    auto hello = build_and_parse(BrowserProfile::Chrome133, seed);

    for (const auto &ext : hello.extensions) {
      if (is_grease_value(ext.type)) {
        // Chrome GREASE extensions at type positions carry either zero-length
        // or one-byte bodies. The first and last GREASE extension in Chrome
        // typically has a 1-byte body (0x00) or zero-length. We accept both.
        ASSERT_TRUE(ext.value.size() <= 1u);
        // If the body is exactly 1 byte, it must be 0x00
        if (ext.value.size() == 1u) {
          ASSERT_EQ(static_cast<td::uint8>(0x00), static_cast<td::uint8>(ext.value[0]));
        }
      }
    }

    // GREASE in key_share entries: body length must be exactly 1, body = 0x00.
    for (const auto &entry : hello.key_share_entries) {
      if (is_grease_value(entry.group)) {
        ASSERT_EQ(static_cast<td::uint16>(1), entry.key_length);
        ASSERT_EQ(static_cast<size_t>(1), entry.key_data.size());
        ASSERT_EQ(static_cast<td::uint8>(0x00), static_cast<td::uint8>(entry.key_data[0]));
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 6. Normalized (GREASE-stripped) extension type list is invariant to
//    GREASE value changes but IS sensitive to GREASE slot addition/removal.
// ---------------------------------------------------------------------------
TEST(TlsGreaseSlotValidityContract, NormalizedNonGreaseEqualityDoesNotHideGreaseDrift) {
  // Build two ClientHellos with different seeds -- the GREASE values will
  // differ but the non-GREASE extension type spine must be identical.
  auto hello_a = build_and_parse(BrowserProfile::Chrome133, 0);
  auto hello_b = build_and_parse(BrowserProfile::Chrome133, 99);

  // Extract non-GREASE extension types from both.
  td::vector<td::uint16> non_grease_a;
  td::vector<td::uint16> non_grease_b;
  for (const auto &ext : hello_a.extensions) {
    if (!is_grease_value(ext.type)) {
      non_grease_a.push_back(ext.type);
    }
  }
  for (const auto &ext : hello_b.extensions) {
    if (!is_grease_value(ext.type)) {
      non_grease_b.push_back(ext.type);
    }
  }

  // The non-GREASE spine must be identical across different seeds.
  ASSERT_EQ(non_grease_a.size(), non_grease_b.size());
  for (size_t i = 0; i < non_grease_a.size(); i++) {
    ASSERT_EQ(non_grease_a[i], non_grease_b[i]);
  }

  // But the full extension list (including GREASE) must expose the GREASE
  // positions. Verify GREASE appears at specific indices by counting.
  size_t grease_count_a = 0;
  size_t grease_count_b = 0;
  for (const auto &ext : hello_a.extensions) {
    if (is_grease_value(ext.type)) {
      grease_count_a++;
    }
  }
  for (const auto &ext : hello_b.extensions) {
    if (is_grease_value(ext.type)) {
      grease_count_b++;
    }
  }

  // Both must have the same number of GREASE extensions.
  ASSERT_EQ(grease_count_a, grease_count_b);
  // Chrome always has exactly 2 GREASE extension slots (first and last).
  ASSERT_EQ(2u, grease_count_a);

  // Simulate GREASE drift: if we removed one GREASE slot from hello_a,
  // the total extension count would change, which is NOT hidden by the
  // non-GREASE normalization alone.
  size_t total_a = hello_a.extensions.size();
  size_t total_stripped = non_grease_a.size();
  ASSERT_TRUE(total_a > total_stripped);
  // The difference must equal the number of GREASE slots.
  ASSERT_EQ(grease_count_a, total_a - total_stripped);
}

// ---------------------------------------------------------------------------
// 7. GREASE slot positions in extensions are stable: first and last
//    (pre-padding) for Chrome, not middle.
// ---------------------------------------------------------------------------
TEST(TlsGreaseSlotValidityContract, GreaseSlotPositionsAreFirstAndLastOnly) {
  for (td::uint64 seed = 0; seed < 100; seed++) {
    auto hello = build_and_parse(BrowserProfile::Chrome133, seed);
    ASSERT_TRUE(hello.extensions.size() >= 3u);

    // Collect indices of GREASE extensions.
    td::vector<size_t> grease_indices;
    for (size_t i = 0; i < hello.extensions.size(); i++) {
      if (is_grease_value(hello.extensions[i].type)) {
        grease_indices.push_back(i);
      }
    }

    // Chrome must have exactly 2 GREASE extension slots.
    ASSERT_EQ(2u, grease_indices.size());

    // First GREASE must be at index 0.
    ASSERT_EQ(0u, grease_indices[0]);

    // Second GREASE must be at the last position or second-to-last
    // (if padding 0x0015 is the actual last extension).
    size_t last_idx = hello.extensions.size() - 1;
    if (hello.extensions[last_idx].type == 0x0015) {
      ASSERT_EQ(last_idx - 1, grease_indices[1]);
    } else {
      ASSERT_EQ(last_idx, grease_indices[1]);
    }
  }
}

// ---------------------------------------------------------------------------
// 8. GREASE in supported_groups: exactly one, always at position 0,
//    and no accidental GREASE values in the middle or end.
// ---------------------------------------------------------------------------
TEST(TlsGreaseSlotValidityContract, SupportedGroupsHasSingleGreaseAtPositionZero) {
  BrowserProfile profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120};
  for (auto profile : profiles) {
    for (td::uint64 seed = 0; seed < 50; seed++) {
      auto hello = build_and_parse(profile, seed);
      ASSERT_FALSE(hello.supported_groups.empty());

      // First entry must be GREASE.
      ASSERT_TRUE(is_grease_value(hello.supported_groups[0]));

      // No other entry may be GREASE.
      size_t grease_count = 0;
      for (size_t i = 0; i < hello.supported_groups.size(); i++) {
        if (is_grease_value(hello.supported_groups[i])) {
          grease_count++;
          ASSERT_EQ(0u, i);
        }
      }
      ASSERT_EQ(1u, grease_count);
    }
  }
}

}  // namespace
