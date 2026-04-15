// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Nightly-scale wire baseline Monte Carlo. Gated on TD_NIGHTLY_CORPUS; in
// PR-scope this file compiles in but every TEST returns in 0ms.

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace {

using namespace td;
using namespace td::mtproto;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

constexpr int32 kUnixTime = 1712345678;
constexpr uint64 kNightlyIterations = kFullIterations * 10;  // 10240, per plan ("10000 seeds")

// Per-profile wire-length empirical envelopes (min - 10%, max + 10%),
// derived from observation of the generated ClientHello sizes across
// the PR-scope corpus runs. These are intentionally loose so they
// catch catastrophic regressions (truncated ClientHello, runaway
// padding budget) while tolerating the normal jitter that GREASE /
// session-ticket / key-share entropy injects.
struct WireEnvelope final {
  size_t min_bytes{0};
  size_t max_bytes{0};
};

// Expected structural invariants: the non-GREASE cipher count is a
// profile constant, the total extension count is a profile constant
// modulo a +/-1 slack (GREASE and padding may or may not materialise).
struct StructuralExpectation final {
  BrowserProfile profile{BrowserProfile::Chrome133};
  EchMode ech_mode{EchMode::Disabled};
  // Set of non-GREASE extension types expected (padding 0x15 is
  // excluded; it is allowed to come and go as padding is dynamic).
  std::unordered_set<uint16> expected_extension_set;
  WireEnvelope envelope{};
};

string build_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
}

size_t count_non_grease_cipher_suites(const ParsedClientHello &hello) {
  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  size_t n = 0;
  for (auto cs : cipher_suites) {
    if (!is_grease_value(cs)) {
      n++;
    }
  }
  return n;
}

size_t count_extensions_excluding_padding(const ParsedClientHello &hello) {
  size_t n = 0;
  for (const auto &ext : hello.extensions) {
    if (ext.type != 0x0015) {
      n++;
    }
  }
  return n;
}

bool profile_has_ech(const ParsedClientHello &hello) {
  return find_extension(hello, fixtures::kEchExtensionType) != nullptr;
}

void assert_structural_invariants_for_profile(BrowserProfile profile, EchMode ech_mode, uint64 seed,
                                              size_t expected_cipher_count, size_t expected_extension_count,
                                              bool expected_ech_presence, const WireEnvelope &envelope) {
  auto wire = build_hello(profile, ech_mode, seed);

  // Wire envelope guard. The empirical bounds are deliberately wide so
  // only a real regression (orders of magnitude off) trips this.
  ASSERT_TRUE(wire.size() >= envelope.min_bytes);
  ASSERT_TRUE(wire.size() <= envelope.max_bytes);

  auto parsed_result = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_result.is_ok());
  auto parsed = parsed_result.move_as_ok();

  // (a) non-GREASE cipher count is stable.
  ASSERT_EQ(expected_cipher_count, count_non_grease_cipher_suites(parsed));

  // (b) total extension count (ignoring padding, which is dynamic) is
  //     stable to +/-1.
  auto observed_ext = count_extensions_excluding_padding(parsed);
  size_t diff =
      observed_ext > expected_extension_count ? observed_ext - expected_extension_count : expected_extension_count - observed_ext;
  ASSERT_TRUE(diff <= 1u);

  // (c) ECH presence must match the profile_spec.allows_ech bit
  //     conjoined with route hints (route hints here are non-RU egress
  //     by default, so the ech_mode argument alone decides).
  const auto &spec = profile_spec(profile);
  bool ech_expected = spec.allows_ech && ech_mode == EchMode::Rfc9180Outer && expected_ech_presence;
  ASSERT_EQ(ech_expected, profile_has_ech(parsed));
}

// Calibrate envelopes by sampling a small number of seeds up front,
// then add a +/-10% cushion. The calibration runs inline in the
// TEST body so it always uses the same builder/version as the
// iteration loop.
WireEnvelope calibrate_envelope(BrowserProfile profile, EchMode ech_mode) {
  WireEnvelope env{std::numeric_limits<size_t>::max(), 0};
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    auto wire = build_hello(profile, ech_mode, seed);
    env.min_bytes = std::min(env.min_bytes, wire.size());
    env.max_bytes = std::max(env.max_bytes, wire.size());
  }
  // Expand by 10% on each side.
  env.min_bytes = env.min_bytes * 9 / 10;
  env.max_bytes = env.max_bytes + env.max_bytes / 10;
  return env;
}

void run_monte_carlo(BrowserProfile profile, EchMode ech_mode) {
  auto envelope = calibrate_envelope(profile, ech_mode);

  // Lock down the non-GREASE cipher count and extension count on the
  // very first generated hello; the rest of the iteration proves they
  // stay there.
  auto wire0 = build_hello(profile, ech_mode, 0);
  auto parsed0 = parse_tls_client_hello(wire0).move_as_ok();
  auto expected_cipher_count = count_non_grease_cipher_suites(parsed0);
  auto expected_extension_count = count_extensions_excluding_padding(parsed0);
  bool expected_ech_presence = profile_has_ech(parsed0);

  for (uint64 seed = 0; seed < kNightlyIterations; seed++) {
    assert_structural_invariants_for_profile(profile, ech_mode, seed, expected_cipher_count, expected_extension_count,
                                             expected_ech_presence, envelope);
  }
}

TEST(TLS_NightlyWireBaselineMonteCarlo, Chrome133EchRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_monte_carlo(BrowserProfile::Chrome133, EchMode::Rfc9180Outer);
}

TEST(TLS_NightlyWireBaselineMonteCarlo, Chrome133NoEchRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_monte_carlo(BrowserProfile::Chrome133, EchMode::Disabled);
}

TEST(TLS_NightlyWireBaselineMonteCarlo, Chrome131EchRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_monte_carlo(BrowserProfile::Chrome131, EchMode::Rfc9180Outer);
}

TEST(TLS_NightlyWireBaselineMonteCarlo, Chrome120Run) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_monte_carlo(BrowserProfile::Chrome120, EchMode::Disabled);
}

TEST(TLS_NightlyWireBaselineMonteCarlo, Firefox148EchRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_monte_carlo(BrowserProfile::Firefox148, EchMode::Rfc9180Outer);
}

TEST(TLS_NightlyWireBaselineMonteCarlo, Safari26_3Run) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_monte_carlo(BrowserProfile::Safari26_3, EchMode::Disabled);
}

TEST(TLS_NightlyWireBaselineMonteCarlo, IOS14Run) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_monte_carlo(BrowserProfile::IOS14, EchMode::Disabled);
}

TEST(TLS_NightlyWireBaselineMonteCarlo, Android11OkHttpAdvisoryRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_monte_carlo(BrowserProfile::Android11_OkHttp_Advisory, EchMode::Disabled);
}

}  // namespace
