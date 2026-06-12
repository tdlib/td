// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Release-facing wire-length similarity gate. Unlike the nightly Monte Carlo
// suite -- which calibrates its envelope by sampling the generator under test
// (self-referential, generator-stability only) -- this gate bounds the
// generated ClientHello length against the *reviewed fixture* wire-length
// Catalog for the family/lane, and fail-closes when that evidence is
// Unavailable. That fixture anchoring (real browser dump lengths, not the
// generator's own output) is the similarity guarantee the broad self-calibrated
// envelope did not provide.
//
// The bound is expressed in BYTES, derived from the generator's mechanism,
// instead of a broad percentage. The earlier version asserted
// within_wire_length_envelope(size, 15.0); on these wires a flat 15% admits
// lengths that appear in no reviewed dump (e.g. firefox accepted ~1606..2545
// against an observed {1890..2213}). It replaces the percentage with an
// explicit per-sample byte model:
//
//   |generated_length - some_reviewed_length| <= kPaddingTargetEntropyMaxBytes
//                                                + kSniLengthSlackBytes
//
// Why this gate is intentionally NOT byte-exact (anti-green-washing note, per
// TDD_approach.instructions.md sec 4.4 -- the code is correct, a tolerance-0.0
// test would be the wrong test):
//   TlsHelloBuilder injects 0..255 bytes of per-build padding-target entropy
//   (see td/mtproto/stealth/TlsHelloBuilder.cpp,
//   `config.padding_target_entropy = rng.bounded(256u)`) as a deliberate
//   anti-DPI feature, so the emitted length is non-deterministic across seeds
//   by design; a fixed ClientHello length would itself be a fingerprint. The
//   generated SNI also differs in length from the reviewed capture SNI. A
//   faithfully generated ClientHello of the same structure as a reviewed
//   capture can therefore differ from that capture's length by at most the
//   padding-entropy budget (<=255 B) plus the SNI-length delta -- and by no
//   more. That sum, not an arbitrary 15%, is the bound below.
//
// kSniLengthSlackBytes is fixture-derived: reviewed capture SNIs are <=25 bytes
// (e.g. "kf58p1vqbctehrki.mooo.com") and the gate's test SNIs are 13-14 bytes
// ("www.apple.com" / "www.google.com"), so the SNI-length delta is at most 12
// bytes; rounded up to 16 for the 2-byte length-field accounting and margin.

#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::Slice;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::MockRng;
using td::mtproto::test::baselines::EvidenceFieldStatus;
using td::mtproto::test::baselines::get_baseline;

constexpr td::int32 kUnixTime = 1712345678;
constexpr td::uint64 kSeeds = 128;

// TlsHelloBuilder's documented anti-DPI padding-target entropy: rng.bounded(256u)
// yields 0..255 bytes added per build.
constexpr size_t kPaddingTargetEntropyMaxBytes = 255;
// Fixture-derived SNI-length delta between reviewed captures (<=25 B) and the
// gate's test SNIs (13-14 B); see file header.
constexpr size_t kSniLengthSlackBytes = 16;
// The maximum number of bytes by which a faithfully generated ClientHello of the
// same structure as a reviewed capture can differ from that capture's length.
constexpr size_t kWireLengthByteDelta = kPaddingTargetEntropyMaxBytes + kSniLengthSlackBytes;

void run_fixture_wire_length_gate(Slice family_id, BrowserProfile profile, EchMode ech_mode, Slice sni) {
  const auto *baseline = get_baseline(family_id, Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  // Fail-closed: a release-facing wire-length similarity claim requires reviewed
  // evidence. Unavailable or Mixed must not silently pass.
  ASSERT_TRUE(baseline->wire_lengths_status == EvidenceFieldStatus::Catalog ||
              baseline->wire_lengths_status == EvidenceFieldStatus::Policy);
  ASSERT_FALSE(baseline->set_catalog.observed_wire_lengths.empty());

  FamilyLaneMatcher matcher(*baseline);
  for (td::uint64 seed = 0; seed < kSeeds; seed++) {
    MockRng rng(seed);
    auto wire =
        build_tls_client_hello_for_profile(sni.str(), "0123456789secret", kUnixTime, profile, ech_mode, rng);
    ASSERT_TRUE(matcher.within_wire_length_byte_model(wire.size(), kWireLengthByteDelta));
  }
}

TEST(TlsGeneratorWireLengthFixtureGate, Firefox148WireLengthStaysWithinReviewedFirefoxLinuxByteModel) {
  run_fixture_wire_length_gate(Slice("firefox_linux_desktop"), BrowserProfile::Firefox148, EchMode::Rfc9180Outer,
                               Slice("www.google.com"));
}

TEST(TlsGeneratorWireLengthFixtureGate, IOS14WireLengthStaysWithinReviewedAppleIosByteModel) {
  run_fixture_wire_length_gate(Slice("apple_ios_tls"), BrowserProfile::IOS14, EchMode::Disabled,
                               Slice("www.apple.com"));
}

// Negative coverage: a length far outside the reviewed catalog -- beyond what
// the padding entropy plus SNI delta can explain -- must be rejected. This
// proves the gate is not vacuously true the way a wide percentage envelope can
// be. The bound is per-sample, so a value sitting between two observed clusters
// by more than kWireLengthByteDelta is also rejected.
TEST(TlsGeneratorWireLengthFixtureGate, ByteModelRejectsLengthsBeyondEntropyBudget) {
  const auto *apple = get_baseline(Slice("apple_ios_tls"), Slice("non_ru_egress"));
  ASSERT_TRUE(apple != nullptr);
  ASSERT_FALSE(apple->set_catalog.observed_wire_lengths.empty());
  FamilyLaneMatcher matcher(*apple);

  // Largest reviewed apple length is 1543; 1543 + kWireLengthByteDelta is the
  // ceiling a faithful hello can reach. One byte past it must fail.
  ASSERT_FALSE(matcher.within_wire_length_byte_model(1543 + kWireLengthByteDelta + 1, kWireLengthByteDelta));
  // A mid-range value (gap between the 512 and 1540 clusters) more than the byte
  // budget from every observed sample must fail; the old 15% envelope would also
  // reject this, but the byte model rejects it for an explainable reason.
  ASSERT_FALSE(matcher.within_wire_length_byte_model(1000, kWireLengthByteDelta));
  // Sanity: an observed length itself, and any length within budget of it, pass.
  ASSERT_TRUE(matcher.within_wire_length_byte_model(1540, kWireLengthByteDelta));
  ASSERT_TRUE(matcher.within_wire_length_byte_model(1543 + kWireLengthByteDelta, kWireLengthByteDelta));
}

}  // namespace
