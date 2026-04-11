// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
//
// REG-3 — Profile-aware ALPS extension wire type invariants.
//
// The TLS ApplicationSettings (ALPS) extension uses two different IANA
// codepoints across Chrome history:
//   * 0x4469 — Chrome 106..132 (legacy ALPS, used by Chrome 120 / 131)
//   * 0x44CD — Chrome 133+ (current ALPS, used by Chrome 133)
//
// Real Chrome wire format for ALPS is:
//   ext_type        (2 bytes, profile-specific: 0x4469 or 0x44CD)
//   ext_body_len    (2 bytes)
//     supported_protocols_list_len (2 bytes)
//       protocol_len  (1 byte)
//       protocol      (N bytes)
//
// Total wire bytes for the canonical "h2"-only ALPS body of 5 bytes:
//   0x44 0x69|0xCD   <- ext type (profile-specific)
//   0x00 0x05        <- body length (5 bytes)
//   0x00 0x03        <- list length
//   0x02             <- proto length
//   0x68 0x32        <- "h2"
//
// Adversarial regression scenarios this suite catches:
//
//   A1. Hardcoded ext type. If a refactor pins the ALPS ext type to one
//       fixed value (e.g. always 0x44CD via the BrowserProfile.h enum
//       constant `ApplicationSettings = 17613`), Chrome 120 and Chrome 131
//       wire-images would advertise the wrong, profile-inconsistent ALPS
//       codepoint and become a unique cross-version distinguishing feature
//       that no real Chrome ever produces.
//
//   A2. Payload smuggling. If the mapper accidentally writes the alps_type
//       *into* the extension payload (in addition to or instead of the wire
//       header), the body length grows by 2 bytes and contains a phantom
//       prefix. BoringSSL strict parsers reject the malformed body, and DPI
//       can fingerprint the duplicated header pattern.
//
//   A3. Drift between supported_groups and ALPS. ALPS is a Chrome-only
//       extension; if a non-Chrome profile (Firefox, Safari, iOS Apple TLS)
//       starts emitting ALPS the wire family becomes incoherent.
//
//   A4. ALPS body content drift. The "h2" application-layer protocol list
//       must remain byte-exact (`\x00\x03\x02\x68\x32`); any reordering or
//       extra bytes is a fingerprint.
//
// Each test in this file is written to fail loud and immediately if the
// behaviour drifts in any of those directions, on a black-hat assumption
// that a future refactor will not be careful with profile coherence.
//
// Pure invariant checks — no statistical sampling needed because the wire
// type is deterministic per profile, but the suite still runs every test
// across multiple seeds to ensure the bug is not gated on a specific RNG
// state.

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
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
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::ParsedClientHello;
using td::mtproto::test::ParsedExtension;
using td::mtproto::test::fixtures::kAlpsChrome131;
using td::mtproto::test::fixtures::kAlpsChrome133Plus;

constexpr td::int32 kFixedUnixTime = 1712345678;
constexpr td::Slice kSecret = td::Slice("0123456789secret");
constexpr td::Slice kHost = td::Slice("www.google.com");

constexpr td::uint16 kAlpsBodyListLength = 0x0003;
constexpr td::uint8 kAlpsBodyProtoLength = 0x02;

// "h2" canonical ALPS body for both Chrome 120/131 (0x4469) and Chrome 133
// (0x44CD). Format: list_len(2) + proto_len(1) + "h2".
constexpr const char kAlpsBodyH2[] = "\x00\x03\x02h2";
constexpr size_t kAlpsBodyH2Length = 5;

// `ParsedClientHello` carries non-owning `Slice`s into the wire buffer it
// was parsed from. Returning a `ParsedClientHello` from a helper that owns
// the wire as a local variable would leak dangling pointers into freed
// heap memory (visible in MSVC Debug builds as `0xDD` paint bytes). The
// owning wrapper keeps the wire buffer alive for the lifetime of the parse
// result.
struct OwnedParsedClientHello final {
  std::string wire;
  ParsedClientHello hello;
};

OwnedParsedClientHello build_and_parse(BrowserProfile profile, EchMode ech_mode, td::uint64 seed) {
  MockRng rng(seed);
  OwnedParsedClientHello result;
  result.wire = build_tls_client_hello_for_profile(kHost.str(), kSecret, kFixedUnixTime, profile, ech_mode, rng);
  auto parsed = parse_tls_client_hello(result.wire);
  if (parsed.is_error()) {
    LOG(ERROR) << "parse failed for profile " << static_cast<int>(profile) << " seed " << seed
               << ": " << parsed.error();
  }
  CHECK(parsed.is_ok());
  result.hello = parsed.move_as_ok();
  return result;
}

const ParsedExtension *find_alps(const ParsedClientHello &hello) {
  if (auto *legacy = find_extension(hello, kAlpsChrome131)) {
    return legacy;
  }
  return find_extension(hello, kAlpsChrome133Plus);
}

void assert_alps_body_is_canonical_h2(const ParsedExtension &ext) {
  ASSERT_EQ(kAlpsBodyH2Length, ext.value.size());
  ASSERT_EQ(td::Slice(kAlpsBodyH2, kAlpsBodyH2Length), ext.value);
}

// A1 + A2 — Chrome 120 must advertise legacy ALPS (0x4469) with a clean
// 5-byte body. No 0x44CD anywhere; no payload prefix smuggling.
TEST(AlpsExtensionWireType, Chrome120UsesLegacyAlpsCodepointWithCleanBody) {
  for (td::uint64 seed : {1u, 7u, 42u, 1024u, 65535u}) {
    auto owned = build_and_parse(BrowserProfile::Chrome120, EchMode::Rfc9180Outer, seed);

    const auto *legacy_alps = find_extension(owned.hello, kAlpsChrome131);
    const auto *modern_alps = find_extension(owned.hello, kAlpsChrome133Plus);
    ASSERT_TRUE(legacy_alps != nullptr);
    ASSERT_TRUE(modern_alps == nullptr);

    assert_alps_body_is_canonical_h2(*legacy_alps);
  }
}

// A1 + A2 — Chrome 131 must advertise legacy ALPS (0x4469) with a clean
// 5-byte body. Identical wire shape to Chrome 120.
TEST(AlpsExtensionWireType, Chrome131UsesLegacyAlpsCodepointWithCleanBody) {
  for (td::uint64 seed : {2u, 11u, 53u, 999u, 0xDEADBEEFu}) {
    auto owned = build_and_parse(BrowserProfile::Chrome131, EchMode::Rfc9180Outer, seed);

    const auto *legacy_alps = find_extension(owned.hello, kAlpsChrome131);
    const auto *modern_alps = find_extension(owned.hello, kAlpsChrome133Plus);
    ASSERT_TRUE(legacy_alps != nullptr);
    ASSERT_TRUE(modern_alps == nullptr);

    assert_alps_body_is_canonical_h2(*legacy_alps);
  }
}

// A1 + A2 — Chrome 133 must advertise the new ALPS (0x44CD) with a clean
// 5-byte body. The legacy ALPS (0x4469) MUST NOT appear.
TEST(AlpsExtensionWireType, Chrome133UsesModernAlpsCodepointWithCleanBody) {
  for (td::uint64 seed : {3u, 17u, 71u, 4096u, 0xCAFEBABEu}) {
    auto owned = build_and_parse(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);

    const auto *legacy_alps = find_extension(owned.hello, kAlpsChrome131);
    const auto *modern_alps = find_extension(owned.hello, kAlpsChrome133Plus);
    ASSERT_TRUE(modern_alps != nullptr);
    ASSERT_TRUE(legacy_alps == nullptr);

    assert_alps_body_is_canonical_h2(*modern_alps);
  }
}

// A1 + A2 — Chrome family ALPS body byte-for-byte equals canonical Chrome
// wire image. Adversarial: the ALPS body must contain only the h2 protocol
// list, with no extra prefix bytes leaked from the alps_type config.
TEST(AlpsExtensionWireType, ChromeAlpsBodyDoesNotContainAlpsTypePrefixBytes) {
  for (auto profile : {BrowserProfile::Chrome120, BrowserProfile::Chrome131, BrowserProfile::Chrome133}) {
    auto owned = build_and_parse(profile, EchMode::Rfc9180Outer, 0xA15D);
    const auto *alps = find_alps(owned.hello);
    ASSERT_TRUE(alps != nullptr);

    // The body must be exactly 5 bytes: list_len(2) + proto_len(1) + "h2".
    ASSERT_EQ(static_cast<size_t>(5), alps->value.size());

    // Adversarial: catch the historical bug where `config.alps_type` was
    // also written into the body as a 2-byte prefix.
    auto first_two = alps->value.substr(0, 2);
    ASSERT_TRUE(first_two != td::Slice("\x44\x69", 2));
    ASSERT_TRUE(first_two != td::Slice("\x44\xcd", 2));

    // The body must not contain any byte sequence that looks like
    // `\x44\x69` or `\x44\xCD` (the two known ALPS codepoints) at any
    // offset. A real h2-only ALPS body never contains these byte pairs.
    for (size_t i = 0; i + 1 < alps->value.size(); i++) {
      auto pair = alps->value.substr(i, 2);
      ASSERT_TRUE(pair != td::Slice("\x44\x69", 2));
      ASSERT_TRUE(pair != td::Slice("\x44\xcd", 2));
    }
  }
}

// A3 — non-Chrome profiles MUST NOT advertise ALPS at all (neither codepoint).
//      Firefox, Safari, iOS, Android profiles do not support ALPS.
TEST(AlpsExtensionWireType, NonChromeProfilesDoNotAdvertiseAlpsAtEither0x4469Or0x44CD) {
  for (auto profile : {BrowserProfile::Firefox148, BrowserProfile::Firefox149_MacOS26_3,
                       BrowserProfile::Safari26_3, BrowserProfile::IOS14,
                       BrowserProfile::Android11_OkHttp_Advisory}) {
    auto owned = build_and_parse(profile, EchMode::Disabled, 0xB16D);
    ASSERT_TRUE(find_extension(owned.hello, kAlpsChrome131) == nullptr);
    ASSERT_TRUE(find_extension(owned.hello, kAlpsChrome133Plus) == nullptr);
  }
}

// A4 — ALPS body content stability: every Chrome profile must produce the
// same canonical h2 body across many seeds. Catches RNG-leaking-into-body
// regressions.
TEST(AlpsExtensionWireType, ChromeAlpsBodyIsSeedIndependent) {
  for (auto profile : {BrowserProfile::Chrome120, BrowserProfile::Chrome131, BrowserProfile::Chrome133}) {
    std::set<std::string> bodies;
    for (td::uint64 seed = 0; seed < 32; seed++) {
      auto owned = build_and_parse(profile, EchMode::Rfc9180Outer, seed);
      const auto *alps = find_alps(owned.hello);
      ASSERT_TRUE(alps != nullptr);
      bodies.insert(alps->value.str());
    }
    ASSERT_EQ(static_cast<size_t>(1), bodies.size());
  }
}

// A1 — Chrome family ALPS codepoint stability across seeds: Chrome 120/131
// always emit 0x4469, Chrome 133 always emits 0x44CD. Catches "ALPS type
// flaps between codepoints depending on RNG" regressions.
TEST(AlpsExtensionWireType, ChromeAlpsCodepointIsSeedIndependent) {
  for (td::uint64 seed = 0; seed < 16; seed++) {
    {
      auto owned = build_and_parse(BrowserProfile::Chrome120, EchMode::Rfc9180Outer, seed);
      ASSERT_TRUE(find_extension(owned.hello, kAlpsChrome131) != nullptr);
      ASSERT_TRUE(find_extension(owned.hello, kAlpsChrome133Plus) == nullptr);
    }
    {
      auto owned = build_and_parse(BrowserProfile::Chrome131, EchMode::Rfc9180Outer, seed);
      ASSERT_TRUE(find_extension(owned.hello, kAlpsChrome131) != nullptr);
      ASSERT_TRUE(find_extension(owned.hello, kAlpsChrome133Plus) == nullptr);
    }
    {
      auto owned = build_and_parse(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
      ASSERT_TRUE(find_extension(owned.hello, kAlpsChrome133Plus) != nullptr);
      ASSERT_TRUE(find_extension(owned.hello, kAlpsChrome131) == nullptr);
    }
  }
}

// Defensive — ECH-disabled path must yield the same ALPS codepoint as
// ECH-enabled. ECH should not bleed into ALPS selection.
TEST(AlpsExtensionWireType, AlpsCodepointIsIndependentOfEchMode) {
  for (auto profile : {BrowserProfile::Chrome120, BrowserProfile::Chrome131, BrowserProfile::Chrome133}) {
    auto with_ech = build_and_parse(profile, EchMode::Rfc9180Outer, 99);
    auto without_ech = build_and_parse(profile, EchMode::Disabled, 99);

    bool with_ech_legacy = find_extension(with_ech.hello, kAlpsChrome131) != nullptr;
    bool without_ech_legacy = find_extension(without_ech.hello, kAlpsChrome131) != nullptr;
    ASSERT_EQ(with_ech_legacy, without_ech_legacy);

    bool with_ech_modern = find_extension(with_ech.hello, kAlpsChrome133Plus) != nullptr;
    bool without_ech_modern = find_extension(without_ech.hello, kAlpsChrome133Plus) != nullptr;
    ASSERT_EQ(with_ech_modern, without_ech_modern);
  }
}

}  // namespace
