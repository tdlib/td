// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Release-facing exact-field similarity gate. For each release-critical
// family/lane this suite first asserts that the reviewed evidence status for
// cipher suites, extension set, and supported versions is enforceable
// (Exact, Catalog, or Policy -- never Unavailable or Mixed), then drives the
// generator over many seeds and requires every emitted ClientHello to match
// the reviewed evidence *for the field's status*:
//   Exact   -> the generated value equals the reviewed exact invariant;
//   Catalog -> the generated value is a member of the reviewed observed catalog
//              (sources legitimately disagree, so there is no single invariant);
//   Policy  -> a named policy matcher (none defined yet -> fail closed).
// The earlier version of this gate called matches_exact_invariants() only,
// which skips any field whose exact invariant is empty -- precisely the
// Catalog-status fields (apple cipher/groups/versions/alpn, chromium/firefox
// extension set). Those fields could drift away from the reviewed dumps without
// failing the gate. matches_release_critical_field() closes that hole by
// requiring catalog membership for Catalog-status fields. The mutant tests at
// the end prove that a wrong cipher suite list, extension set, or supported
// versions list fails for both Exact and Catalog status.
//
// Unlike the self-calibrated nightly Monte Carlo suites, the oracle here is
// fixture-derived, so a generator drift away from real browser dumps fails the
// gate instead of silently recalibrating.
//
// ECH mode per family is chosen to match the reviewed evidence: chromium and
// firefox Linux desktop dumps carry ECH (ech_presence_required=true), so they
// run with EchMode::Rfc9180Outer; apple_ios_tls dumps have no ECH, so it runs
// with EchMode::Disabled to keep the non-GREASE extension set equal to the
// reviewed 13-extension set.

#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <memory>
#include <utility>

namespace {

using td::Slice;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::MockRng;
using td::mtproto::test::ParsedClientHello;
using td::mtproto::test::ParsedExtension;
using td::mtproto::test::ReleaseCriticalField;
using td::mtproto::test::baselines::EvidenceFieldStatus;
using td::mtproto::test::baselines::get_baseline;
using td::mtproto::test::parse_tls_client_hello;

constexpr td::int32 kUnixTime = 1712345678;
constexpr td::uint64 kSeeds = 64;

void assert_status_is_enforceable(EvidenceFieldStatus status) {
  ASSERT_TRUE(status == EvidenceFieldStatus::Exact || status == EvidenceFieldStatus::Catalog ||
              status == EvidenceFieldStatus::Policy);
}

ParsedClientHello build_and_parse(Slice sni, BrowserProfile profile, EchMode ech_mode, td::uint64 seed) {
  MockRng rng(seed);
  auto wire = build_tls_client_hello_for_profile(sni.str(), "0123456789secret", kUnixTime, profile, ech_mode, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

void run_exact_gate(Slice family_id, BrowserProfile profile, EchMode ech_mode) {
  const auto *baseline = get_baseline(family_id, Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  assert_status_is_enforceable(baseline->non_grease_cipher_suites_status);
  assert_status_is_enforceable(baseline->non_grease_extension_set_status);
  assert_status_is_enforceable(baseline->non_grease_supported_versions_status);

  FamilyLaneMatcher matcher(*baseline);
  for (td::uint64 seed = 0; seed < kSeeds; seed++) {
    auto parsed = build_and_parse(Slice("www.google.com"), profile, ech_mode, seed);
    // matches_exact_invariants still enforces every populated exact invariant
    // (e.g. ech presence, apple's exact 13-extension set, compress algorithms).
    ASSERT_TRUE(matcher.matches_exact_invariants(parsed));
    // Status-dispatched checks additionally enforce Catalog-status fields that
    // matches_exact_invariants skips because their exact invariant is empty.
    ASSERT_TRUE(matcher.matches_release_critical_field(parsed, ReleaseCriticalField::CipherSuites));
    ASSERT_TRUE(matcher.matches_release_critical_field(parsed, ReleaseCriticalField::ExtensionSet));
    ASSERT_TRUE(matcher.matches_release_critical_field(parsed, ReleaseCriticalField::SupportedVersions));
  }
}

TEST(TlsGeneratorFixtureExactFieldsGate, Chrome133MatchesChromiumLinuxReviewedExactFields) {
  run_exact_gate(Slice("chromium_linux_desktop"), BrowserProfile::Chrome133, EchMode::Rfc9180Outer);
}

TEST(TlsGeneratorFixtureExactFieldsGate, Firefox148MatchesFirefoxLinuxReviewedExactFields) {
  run_exact_gate(Slice("firefox_linux_desktop"), BrowserProfile::Firefox148, EchMode::Rfc9180Outer);
}

TEST(TlsGeneratorFixtureExactFieldsGate, IOS14MatchesAppleIosReviewedExactFields) {
  run_exact_gate(Slice("apple_ios_tls"), BrowserProfile::IOS14, EchMode::Disabled);
}

// --- Status coverage: the three release families exercise both Exact and
// Catalog status for the gated fields, so the gate above is not vacuously
// checking Exact-only data. -----------------------------------------------

TEST(TlsGeneratorFixtureExactFieldsGate, ReleaseFamiliesCoverExactAndCatalogStatus) {
  const auto *apple = get_baseline(Slice("apple_ios_tls"), Slice("non_ru_egress"));
  const auto *chromium = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  const auto *firefox = get_baseline(Slice("firefox_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(apple != nullptr && chromium != nullptr && firefox != nullptr);

  // Catalog-status fields (the regression target: previously unchecked).
  ASSERT_TRUE(apple->non_grease_cipher_suites_status == EvidenceFieldStatus::Catalog);
  ASSERT_TRUE(apple->non_grease_supported_versions_status == EvidenceFieldStatus::Catalog);
  ASSERT_TRUE(chromium->non_grease_extension_set_status == EvidenceFieldStatus::Catalog);
  ASSERT_TRUE(firefox->non_grease_cipher_suites_status == EvidenceFieldStatus::Catalog);
  ASSERT_TRUE(firefox->non_grease_extension_set_status == EvidenceFieldStatus::Catalog);

  // Exact-status fields still exist, so both branches of the dispatch run.
  ASSERT_TRUE(apple->non_grease_extension_set_status == EvidenceFieldStatus::Exact);
  ASSERT_TRUE(chromium->non_grease_cipher_suites_status == EvidenceFieldStatus::Exact);
  ASSERT_TRUE(chromium->non_grease_supported_versions_status == EvidenceFieldStatus::Exact);

  // Catalog-status fields must carry a non-empty observed catalog, otherwise the
  // membership check would degrade to an always-false (or always-skip) no-op.
  ASSERT_FALSE(apple->set_catalog.observed_cipher_suite_sequences.empty());
  ASSERT_FALSE(apple->set_catalog.observed_supported_versions_sequences.empty());
  ASSERT_FALSE(chromium->set_catalog.observed_extension_sets.empty());
  ASSERT_FALSE(firefox->set_catalog.observed_cipher_suite_sequences.empty());
  ASSERT_FALSE(firefox->set_catalog.observed_extension_sets.empty());
}

// --- Mutant / negative coverage: a release-critical field must FAIL when the
// generated value does not match the reviewed evidence. Cross-family helos
// supply a real, structurally valid ClientHello whose values are off-catalog
// for the baseline under test; the supported_versions mutant is synthesised
// directly because every release family shares {0x0304, 0x0303}. -----------

// Exact-status cipher suites reject a foreign cipher list.
TEST(TlsGeneratorFixtureExactFieldsGate, ChromiumExactCipherRejectsAppleCipherList) {
  const auto *chromium = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(chromium != nullptr);
  ASSERT_TRUE(chromium->non_grease_cipher_suites_status == EvidenceFieldStatus::Exact);
  FamilyLaneMatcher matcher(*chromium);
  auto apple_hello = build_and_parse(Slice("www.apple.com"), BrowserProfile::IOS14, EchMode::Disabled, 7);
  ASSERT_FALSE(matcher.matches_release_critical_field(apple_hello, ReleaseCriticalField::CipherSuites));
}

// Catalog-status cipher suites reject a foreign cipher list.
TEST(TlsGeneratorFixtureExactFieldsGate, AppleCatalogCipherRejectsChromiumCipherList) {
  const auto *apple = get_baseline(Slice("apple_ios_tls"), Slice("non_ru_egress"));
  ASSERT_TRUE(apple != nullptr);
  ASSERT_TRUE(apple->non_grease_cipher_suites_status == EvidenceFieldStatus::Catalog);
  FamilyLaneMatcher matcher(*apple);
  auto chromium_hello = build_and_parse(Slice("www.google.com"), BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 7);
  ASSERT_FALSE(matcher.matches_release_critical_field(chromium_hello, ReleaseCriticalField::CipherSuites));
}

// Exact-status extension set rejects a foreign extension set.
TEST(TlsGeneratorFixtureExactFieldsGate, AppleExactExtensionSetRejectsChromiumExtensions) {
  const auto *apple = get_baseline(Slice("apple_ios_tls"), Slice("non_ru_egress"));
  ASSERT_TRUE(apple != nullptr);
  ASSERT_TRUE(apple->non_grease_extension_set_status == EvidenceFieldStatus::Exact);
  FamilyLaneMatcher matcher(*apple);
  auto chromium_hello = build_and_parse(Slice("www.google.com"), BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 11);
  ASSERT_FALSE(matcher.matches_release_critical_field(chromium_hello, ReleaseCriticalField::ExtensionSet));
}

// Catalog-status extension set rejects a foreign extension set.
TEST(TlsGeneratorFixtureExactFieldsGate, ChromiumCatalogExtensionSetRejectsAppleExtensions) {
  const auto *chromium = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(chromium != nullptr);
  ASSERT_TRUE(chromium->non_grease_extension_set_status == EvidenceFieldStatus::Catalog);
  FamilyLaneMatcher matcher(*chromium);
  auto apple_hello = build_and_parse(Slice("www.apple.com"), BrowserProfile::IOS14, EchMode::Disabled, 11);
  ASSERT_FALSE(matcher.matches_release_critical_field(apple_hello, ReleaseCriticalField::ExtensionSet));
}

// Builds a parsed ClientHello carrying exactly `versions` (each a 2-byte
// big-endian value) inside a supported_versions (0x002B) extension. The buffer
// is owned by `owned_wire` so the extension Slice stays valid after the value
// is returned (see the lifecycle note on ParsedClientHello).
ParsedClientHello make_supported_versions_hello(const td::vector<td::uint16> &versions) {
  ParsedClientHello hello;
  auto buf = std::make_unique<td::string>();
  buf->push_back(static_cast<char>(versions.size() * 2));
  for (auto version : versions) {
    buf->push_back(static_cast<char>((version >> 8) & 0xFF));
    buf->push_back(static_cast<char>(version & 0xFF));
  }
  hello.owned_wire = std::move(buf);
  ParsedExtension ext;
  ext.type = 0x002Bu;
  ext.value = Slice(hello.owned_wire->data(), hello.owned_wire->size());
  hello.extensions.push_back(ext);
  return hello;
}

// Off-catalog supported_versions fail for both Exact and Catalog status.
TEST(TlsGeneratorFixtureExactFieldsGate, OffCatalogSupportedVersionsRejectedForBothStatuses) {
  // Sanity: the synthetic builder produces a value the real reviewed evidence
  // accepts, so a rejection below is about the value, not the construction.
  const auto *chromium = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  const auto *apple = get_baseline(Slice("apple_ios_tls"), Slice("non_ru_egress"));
  ASSERT_TRUE(chromium != nullptr && apple != nullptr);
  ASSERT_TRUE(chromium->non_grease_supported_versions_status == EvidenceFieldStatus::Exact);
  ASSERT_TRUE(apple->non_grease_supported_versions_status == EvidenceFieldStatus::Catalog);

  FamilyLaneMatcher chromium_matcher(*chromium);
  FamilyLaneMatcher apple_matcher(*apple);

  auto valid = make_supported_versions_hello({0x0304u, 0x0303u});
  ASSERT_TRUE(chromium_matcher.matches_release_critical_field(valid, ReleaseCriticalField::SupportedVersions));
  ASSERT_TRUE(apple_matcher.matches_release_critical_field(valid, ReleaseCriticalField::SupportedVersions));

  // 0x0305 is not a real TLS version and appears in no reviewed dump.
  auto bogus = make_supported_versions_hello({0x0305u});
  ASSERT_FALSE(chromium_matcher.matches_release_critical_field(bogus, ReleaseCriticalField::SupportedVersions));
  ASSERT_FALSE(apple_matcher.matches_release_critical_field(bogus, ReleaseCriticalField::SupportedVersions));
}

}  // namespace
