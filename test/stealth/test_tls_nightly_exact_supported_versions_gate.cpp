// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Nightly Monte Carlo: exact supported_versions (0x002B) gate.
// For every generated ClientHello the non-GREASE entries in the
// supported_versions extension must match the profile spec exactly
// (order, count, values).  Gated on TD_NIGHTLY_CORPUS; in PR-scope
// this file compiles in but every TEST returns in 0ms.

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

using namespace td;
using namespace td::mtproto;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

constexpr int32 kUnixTime = 1712345678;
constexpr uint64 kNightlyIterations = kFullIterations * 10;  // 10240

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

string build_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
}

// Parse the raw supported_versions extension body from a ClientHello into
// an ordered vector of version uint16 values.  The wire format is:
//   u8  list_length          (total bytes for the version list)
//   u16 version[list_length/2]
Result<vector<uint16>> parse_supported_versions_list(Slice data) {
  TlsReader reader(data);
  TRY_RESULT(list_len, reader.read_u8());
  if ((list_len % 2) != 0) {
    return Status::Error("supported_versions list length must be even");
  }
  if (reader.left() != list_len) {
    return Status::Error("supported_versions length mismatch");
  }
  vector<uint16> versions;
  versions.reserve(list_len / 2);
  while (reader.left() > 0) {
    TRY_RESULT(version, reader.read_u16());
    versions.push_back(version);
  }
  return versions;
}

// Extract only the non-GREASE entries, preserving order.
vector<uint16> non_grease_versions(const vector<uint16> &versions) {
  vector<uint16> result;
  result.reserve(versions.size());
  for (auto v : versions) {
    if (!is_grease_value(v)) {
      result.push_back(v);
    }
  }
  return result;
}

// Return the expected non-GREASE supported_versions list for a profile,
// derived from the BrowserProfileSpec extensions vector.
vector<uint16> expected_versions_for_profile(BrowserProfile profile) {
  const auto &spec = get_profile_spec(profile);
  for (const auto &ext : spec.extensions) {
    if (ext.type == TlsExtensionType::SupportedVersions) {
      return ext.u16_list;
    }
  }
  // No SupportedVersions extension in profile spec — should never happen.
  UNREACHABLE();
  return {};
}

// Whether the profile spec prepends a GREASE entry before the real
// version list.
bool profile_has_grease_versions(BrowserProfile profile) {
  const auto &spec = get_profile_spec(profile);
  for (const auto &ext : spec.extensions) {
    if (ext.type == TlsExtensionType::SupportedVersions) {
      return ext.prepend_grease;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Core assertion: for a single seed, the supported_versions extension in
// the generated wire must carry exactly the profile's expected non-GREASE
// versions (in order) and, if GREASE is enabled for the profile, the
// first entry must be a valid GREASE codepoint.
// ---------------------------------------------------------------------------

void assert_supported_versions_exact(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  auto wire = build_hello(profile, ech_mode, seed);
  auto parsed_result = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_result.is_ok());
  auto parsed = parsed_result.move_as_ok();

  // The supported_versions extension (0x002B) must always be present.
  auto *sv_ext = find_extension(parsed, 0x002B);
  ASSERT_TRUE(sv_ext != nullptr);

  auto versions_result = parse_supported_versions_list(sv_ext->value);
  ASSERT_TRUE(versions_result.is_ok());
  auto all_versions = versions_result.move_as_ok();

  // --- non-GREASE entries must match the profile spec exactly ---
  auto observed = non_grease_versions(all_versions);
  auto expected = expected_versions_for_profile(profile);
  ASSERT_EQ(expected.size(), observed.size());
  for (size_t i = 0; i < expected.size(); i++) {
    ASSERT_EQ(expected[i], observed[i]);
  }

  // --- GREASE gate ---
  bool expect_grease = profile_has_grease_versions(profile);
  if (expect_grease) {
    // The list must have at least one more entry than the non-GREASE set.
    ASSERT_TRUE(all_versions.size() > expected.size());
    // First entry must be a valid GREASE codepoint.
    ASSERT_TRUE(is_grease_value(all_versions[0]));
  } else {
    // Without GREASE, the full list must equal the non-GREASE list.
    ASSERT_EQ(expected.size(), all_versions.size());
  }
}

void run_supported_versions_monte_carlo(BrowserProfile profile, EchMode ech_mode) {
  for (uint64 seed = 0; seed < kNightlyIterations; seed++) {
    assert_supported_versions_exact(profile, ech_mode, seed);
  }
}

// Covers: RISK-FP-05 (nightly undercoverage)

// ---------------------------------------------------------------------------
// Per-profile nightly Monte Carlo tests
// ---------------------------------------------------------------------------

TEST(TLS_NightlySupportedVersionsGate, Chrome133EchRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_supported_versions_monte_carlo(BrowserProfile::Chrome133, EchMode::Rfc9180Outer);
}

TEST(TLS_NightlySupportedVersionsGate, Chrome133NoEchRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_supported_versions_monte_carlo(BrowserProfile::Chrome133, EchMode::Disabled);
}

TEST(TLS_NightlySupportedVersionsGate, Chrome131EchRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_supported_versions_monte_carlo(BrowserProfile::Chrome131, EchMode::Rfc9180Outer);
}

TEST(TLS_NightlySupportedVersionsGate, Chrome120Run) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_supported_versions_monte_carlo(BrowserProfile::Chrome120, EchMode::Disabled);
}

TEST(TLS_NightlySupportedVersionsGate, Firefox148EchRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_supported_versions_monte_carlo(BrowserProfile::Firefox148, EchMode::Rfc9180Outer);
}

TEST(TLS_NightlySupportedVersionsGate, Safari26_3Run) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_supported_versions_monte_carlo(BrowserProfile::Safari26_3, EchMode::Disabled);
}

TEST(TLS_NightlySupportedVersionsGate, IOS14Run) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_supported_versions_monte_carlo(BrowserProfile::IOS14, EchMode::Disabled);
}

TEST(TLS_NightlySupportedVersionsGate, Android11OkHttpAdvisoryRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_supported_versions_monte_carlo(BrowserProfile::Android11_OkHttp_Advisory, EchMode::Disabled);
}

// ---------------------------------------------------------------------------
// Falsification: swapping Chrome133 expected versions to Firefox order
// (no GREASE) must fail for at least one GREASE-bearing seed.
// ---------------------------------------------------------------------------

TEST(TLS_NightlySupportedVersionsGate, WrongProfileVersionsMismatch) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  // Chrome133 prepends a GREASE entry. If we pretend the expected
  // non-GREASE list is {0x0303, 0x0304} (reversed) or a single
  // {0x0304}, at least one seed must disagree.
  auto wrong_expected = vector<uint16>{0x0303, 0x0304};  // reversed order
  bool found_mismatch = false;
  for (uint64 seed = 0; seed < kNightlyIterations && !found_mismatch; seed++) {
    auto wire = build_hello(BrowserProfile::Chrome133, EchMode::Disabled, seed);
    auto parsed = parse_tls_client_hello(wire).move_as_ok();
    auto *sv_ext = find_extension(parsed, 0x002B);
    ASSERT_TRUE(sv_ext != nullptr);
    auto all_versions = parse_supported_versions_list(sv_ext->value).move_as_ok();
    auto observed = non_grease_versions(all_versions);
    if (observed.size() != wrong_expected.size()) {
      found_mismatch = true;
    } else {
      for (size_t i = 0; i < observed.size(); i++) {
        if (observed[i] != wrong_expected[i]) {
          found_mismatch = true;
          break;
        }
      }
    }
  }
  // The real Chrome133 emits {0x0304, 0x0303} — the reversed list must
  // mismatch on every seed.
  ASSERT_TRUE(found_mismatch);
}

}  // namespace
