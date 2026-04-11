// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
//
// REG-17 — Firefox149_MacOS26_3 PreSharedKey extension invariants.
//
// Real Firefox 149 captures on macOS 26.3 (test/analysis/fixtures/
// clienthello/macos/firefox149_macos26_3.clienthello.json) advertise a
// `pre_shared_key` (PSK) extension as the LAST extension in the
// ClientHello, after the ECH (`0xFE0D`) extension. Real Firefox 149 on
// Linux desktop captures (test/analysis/fixtures/clienthello/linux_desktop/
// firefox149_*.json) DO NOT carry the PSK extension at all.
//
// This is a per-platform distinguishing fingerprint that the rebuilt
// `make_firefox149_macos_impl()` profile MUST emit to be byte-shape-
// equivalent to real macOS Firefox 149. The historical (pre-fix) form of
// the function copied the Linux Firefox 148 extension list verbatim and
// dropped the trailing PSK, leaving a 16-extension wire image where the
// real macOS Firefox 149 capture has 17 extensions and ends in `0x0029`.
//
// The PSK extension wire format is the TLS 1.3 OfferedPsks structure:
//
//   identities_len(2 bytes) +
//     [PskIdentity {opaque identity<1..2^16-1>; uint32 obfuscated_ticket_age;}]
//   binders_len(2 bytes) +
//     [PskBinderEntry {opaque PskBinderEntry<32..255>;}]
//
// Real macOS Firefox 149 captures use:
//   identities_len = 0x006f (111)
//     identity_len = 0x0069 (105)
//     identity     = 105 random bytes
//     obfuscated_ticket_age = 4 bytes
//   binders_len  = 0x0021 (33)
//     binder_len = 0x20 (32)
//     binder     = 32 random bytes
//
// Total body length = 2 + 111 + 2 + 33 = 148 bytes.
//
// We emit the same wire shape with random identity / binder bodies. The
// random bodies are opaque to any DPI middlebox (they look like the same
// session ticket bytes a real Firefox would emit), and the structure is
// valid TLS 1.3, so a strict BoringSSL parser at the server will accept
// the extension and just fail to resume — which is the same behaviour as
// any first-contact Firefox session anyway.

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"

#include <set>

namespace {

using td::mtproto::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::ParsedClientHello;
using td::mtproto::test::ParsedExtension;

constexpr td::int32 kFixedUnixTime = 1712345678;
constexpr td::Slice kSecret = td::Slice("0123456789secret");
constexpr td::Slice kHost = td::Slice("www.google.com");
constexpr td::uint16 kPreSharedKeyType = 0x0029;
constexpr td::uint16 kEchType = 0xFE0D;

constexpr size_t kPskBodyTotalLength = 148;
constexpr td::uint16 kIdentitiesLen = 0x006F;  // 111
constexpr td::uint16 kIdentityLen = 0x0069;    // 105
constexpr td::uint16 kBindersLen = 0x0021;     // 33
constexpr td::uint8 kBinderLen = 0x20;         // 32

ParsedClientHello build_macos_firefox(td::uint64 seed) {
  MockRng rng(seed);
  auto wire = build_tls_client_hello_for_profile(kHost.str(), kSecret, kFixedUnixTime,
                                                 BrowserProfile::Firefox149_MacOS26_3,
                                                 EchMode::Rfc9180Outer, rng);
  auto parsed = parse_tls_client_hello(wire);
  if (parsed.is_error()) {
    LOG(ERROR) << "macOS Firefox 149 parse failed for seed " << seed << ": " << parsed.error();
  }
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

// I1 — Firefox149_MacOS26_3 MUST emit a PSK extension. Linux Firefox 149
// MUST NOT (the platform-distinguishing fingerprint).
TEST(FirefoxMacOsPskExtension, MacOsFirefox149AlwaysCarriesPreSharedKeyExtension) {
  for (td::uint64 seed = 0; seed < 32; seed++) {
    auto hello = build_macos_firefox(seed);
    auto *psk = find_extension(hello, kPreSharedKeyType);
    ASSERT_TRUE(psk != nullptr);
  }
}

// I2 — The PSK extension MUST be the LAST extension in the wire (after
// ECH). Real macOS Firefox 149 captures show this ordering and any DPI
// that sequences extensions can detect a misplaced PSK.
TEST(FirefoxMacOsPskExtension, PskExtensionIsTheLastExtensionInTheClientHello) {
  for (td::uint64 seed = 0; seed < 32; seed++) {
    auto hello = build_macos_firefox(seed);
    ASSERT_FALSE(hello.extensions.empty());
    ASSERT_EQ(static_cast<td::uint16>(kPreSharedKeyType), hello.extensions.back().type);
  }
}

// I3 — The PSK body length MUST be exactly 148 bytes (matching real
// macOS Firefox 149 captures).
TEST(FirefoxMacOsPskExtension, PskBodyLengthMatchesRealMacOsCapture) {
  for (td::uint64 seed = 0; seed < 32; seed++) {
    auto hello = build_macos_firefox(seed);
    auto *psk = find_extension(hello, kPreSharedKeyType);
    ASSERT_TRUE(psk != nullptr);
    ASSERT_EQ(kPskBodyTotalLength, psk->value.size());
  }
}

// I4 — The PSK body MUST decode as a valid TLS 1.3 OfferedPsks structure
// with the canonical macOS Firefox 149 length fields (`0x006F` identities,
// `0x0069` identity, `0x0021` binders, `0x20` binder).
TEST(FirefoxMacOsPskExtension, PskBodyDecodesAsValidOfferedPsksStructure) {
  for (td::uint64 seed = 0; seed < 32; seed++) {
    auto hello = build_macos_firefox(seed);
    auto *psk = find_extension(hello, kPreSharedKeyType);
    ASSERT_TRUE(psk != nullptr);
    ASSERT_EQ(kPskBodyTotalLength, psk->value.size());

    auto body = psk->value;
    auto read_u16_be = [](td::Slice s, size_t off) -> td::uint16 {
      return static_cast<td::uint16>((static_cast<td::uint8>(s[off]) << 8) | static_cast<td::uint8>(s[off + 1]));
    };

    ASSERT_EQ(kIdentitiesLen, read_u16_be(body, 0));
    ASSERT_EQ(kIdentityLen, read_u16_be(body, 2));
    // 105 identity bytes + 4 obfuscated_ticket_age bytes = 109
    // identities_len(2) + identity_len(2) + 109 = 113
    ASSERT_EQ(kBindersLen, read_u16_be(body, 113));
    ASSERT_EQ(kBinderLen, static_cast<td::uint8>(body[115]));
  }
}

// I5 — The PSK identity bytes MUST vary across seeds (the executor
// receives a per-build RNG and re-rolls the random portions on every
// build, otherwise the PSK ext becomes a static fingerprint by itself).
TEST(FirefoxMacOsPskExtension, PskIdentityBytesAreSeedDependent) {
  std::set<std::string> identities;
  for (td::uint64 seed = 0; seed < 16; seed++) {
    auto hello = build_macos_firefox(seed);
    auto *psk = find_extension(hello, kPreSharedKeyType);
    ASSERT_TRUE(psk != nullptr);
    // Identity bytes live at offset 4 (after identities_len(2) +
    // identity_len(2)) and span 105 bytes.
    identities.insert(psk->value.substr(4, 105).str());
  }
  // 16 distinct seeds should produce >= 2 distinct identity blobs.
  ASSERT_TRUE(identities.size() >= 2u);
}

// I6 — The PSK binder bytes MUST also vary across seeds.
TEST(FirefoxMacOsPskExtension, PskBinderBytesAreSeedDependent) {
  std::set<std::string> binders;
  for (td::uint64 seed = 0; seed < 16; seed++) {
    auto hello = build_macos_firefox(seed);
    auto *psk = find_extension(hello, kPreSharedKeyType);
    ASSERT_TRUE(psk != nullptr);
    // Binder bytes live at offset 116 (= 4 identity header + 109 identity body
    // + 2 binders_len + 1 binder_len) and span 32 bytes.
    binders.insert(psk->value.substr(116, 32).str());
  }
  ASSERT_TRUE(binders.size() >= 2u);
}

// I7 — Linux Firefox 148 (and any other non-macOS Firefox) MUST NOT
// carry the PSK extension. This is the platform-discriminating invariant.
TEST(FirefoxMacOsPskExtension, NonMacOsFirefoxProfilesDoNotCarryPskExtension) {
  for (auto profile : {BrowserProfile::Firefox148}) {
    MockRng rng(0xCAFE);
    auto wire = build_tls_client_hello_for_profile(kHost.str(), kSecret, kFixedUnixTime, profile, EchMode::Disabled,
                                                   rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), kPreSharedKeyType) == nullptr);
  }
}

// I8 — The PSK extension MUST come AFTER the ECH extension in the wire
// when both are present, matching the real macOS Firefox 149 capture
// order.
TEST(FirefoxMacOsPskExtension, PskExtensionFollowsEchExtensionInExtensionList) {
  auto hello = build_macos_firefox(0xBEEF);
  bool seen_ech = false;
  bool seen_psk = false;
  bool psk_after_ech = false;
  for (const auto &ext : hello.extensions) {
    if (ext.type == kEchType) {
      seen_ech = true;
    } else if (ext.type == kPreSharedKeyType) {
      seen_psk = true;
      psk_after_ech = seen_ech;
    }
  }
  ASSERT_TRUE(seen_ech);
  ASSERT_TRUE(seen_psk);
  ASSERT_TRUE(psk_after_ech);
}

}  // namespace
