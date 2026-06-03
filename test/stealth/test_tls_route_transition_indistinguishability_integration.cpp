// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Integration tests: RISK-FP-07 -- Route transitions must not emit unique
// probe signatures.  Switching between RU / non-RU routes, toggling ECH
// on/off via circuit-breaker, and falling back from a blocked transport
// must all produce wire bytes that are valid TLS ClientHellos
// indistinguishable from steady-state connections.

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"
#include "td/utils/Time.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <vector>

namespace {

using td::int32;
using td::uint16;
using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::default_runtime_stealth_params;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::get_runtime_ech_decision;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::note_runtime_ech_failure;
using td::mtproto::stealth::reset_runtime_ech_counters_for_tests;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::ParsedClientHello;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

class RuntimeGuard final {
 public:
  RuntimeGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }
};

NetworkRouteHints non_ru_route() {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;
  return route;
}

NetworkRouteHints ru_route() {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = true;
  return route;
}

NetworkRouteHints unknown_route() {
  NetworkRouteHints route;
  route.is_known = false;
  route.is_ru = false;
  return route;
}

bool has_ech_extension(const ParsedClientHello &hello) {
  return find_extension(hello, td::mtproto::test::fixtures::kEchExtensionType) != nullptr;
}

bool is_grease(uint16 type) {
  auto hi = static_cast<td::uint8>((type >> 8) & 0xFF);
  auto lo = static_cast<td::uint8>(type & 0xFF);
  return hi == lo && (hi & 0x0F) == 0x0A;
}

// Extract a canonical set of non-GREASE, non-padding extension types from a
// parsed ClientHello.  This "structural fingerprint" captures the extension
// repertoire without the per-build randomness of GREASE values and padding
// length.
std::vector<uint16> structural_extension_set(const ParsedClientHello &hello) {
  std::vector<uint16> result;
  for (const auto &ext : hello.extensions) {
    if (is_grease(ext.type) || ext.type == 0x0015 /* padding */) {
      continue;
    }
    result.push_back(ext.type);
  }
  std::sort(result.begin(), result.end());
  return result;
}

// Verify that `wire` is a structurally valid TLS ClientHello (record type
// 0x16, handshake type 0x01) by attempting a full parse.  Returns true when
// the parse succeeds and the mandatory structural invariants hold.
bool is_valid_tls_client_hello(td::Slice wire) {
  auto parsed = parse_tls_client_hello(wire);
  if (parsed.is_error()) {
    return false;
  }
  const auto &hello = parsed.ok();
  if (hello.record_type != 0x16) {
    return false;
  }
  if (hello.handshake_type != 0x01) {
    return false;
  }
  if (hello.client_legacy_version != 0x0303) {
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// TEST 1: Switching from non-RU to RU route does not emit a distinguishable
//         transition packet.
// ---------------------------------------------------------------------------

TEST(TlsRouteTransitionIndistinguishability,
     NonRuToRuTransitionDoesNotEmitDistinguishablePacket) {
  td::Slice secret("0123456789secret");
  constexpr int32 kBaseTime = 1712345678;

  // Collect structural fingerprints from steady-state non-RU connections.
  std::vector<std::vector<uint16>> steady_state_non_ru;
  for (int i = 0; i < 64; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, kBaseTime + i, non_ru_route());
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    steady_state_non_ru.push_back(structural_extension_set(parsed.ok()));
  }

  // Collect structural fingerprints from steady-state RU connections.
  std::vector<std::vector<uint16>> steady_state_ru;
  for (int i = 0; i < 64; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, kBaseTime + 100 + i, ru_route());
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    steady_state_ru.push_back(structural_extension_set(parsed.ok()));
  }

  // Now simulate a transition: the last build used non-RU, the next uses RU.
  // The "transition" ClientHello must have the same structural fingerprint as
  // a steady-state RU ClientHello -- no unique marker leaks.
  for (int i = 0; i < 64; i++) {
    // Previous build was non-RU (simulated by the loop above).
    // Transition build:
    auto wire = build_default_tls_client_hello("www.google.com", secret, kBaseTime + 200 + i, ru_route());
    ASSERT_TRUE(is_valid_tls_client_hello(wire));

    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto transition_ext = structural_extension_set(parsed.ok());

    // The transition must produce the same extension repertoire as one of the
    // steady-state RU builds (i.e. no extra marker extension).
    bool matches_some_ru = false;
    for (const auto &ru_ext : steady_state_ru) {
      if (transition_ext == ru_ext) {
        matches_some_ru = true;
        break;
      }
    }
    ASSERT_TRUE(matches_some_ru);

    // The transition must NOT carry ECH (since RU disables it).
    ASSERT_FALSE(has_ech_extension(parsed.ok()));
  }
}

// ---------------------------------------------------------------------------
// TEST 2: ECH-to-no-ECH transition generates a valid ClientHello (not a
//         probe).
// ---------------------------------------------------------------------------

TEST(TlsRouteTransitionIndistinguishability,
     EchToNoEchTransitionGeneratesValidClientHello) {
  RuntimeGuard guard;

  auto params = default_runtime_stealth_params();
  params.route_failure.ech_failure_threshold = 1;
  params.route_failure.ech_disable_ttl_seconds = 600.0;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  td::Slice secret("0123456789secret");
  constexpr int32 kBaseTime = 1712345678;
  const td::string dest = "ech-transition-test.example.com";

  // Phase 1: Build a ClientHello with ECH enabled (non-RU, no failures).
  auto wire_ech_on = build_default_tls_client_hello("www.google.com", secret, kBaseTime, non_ru_route());
  auto parsed_ech_on = parse_tls_client_hello(wire_ech_on);
  ASSERT_TRUE(parsed_ech_on.is_ok());
  ASSERT_TRUE(has_ech_extension(parsed_ech_on.ok()));

  // Phase 2: Trigger the circuit breaker so ECH becomes disabled.
  note_runtime_ech_failure(dest, kBaseTime + 1);
  auto decision = get_runtime_ech_decision(dest, kBaseTime + 2, non_ru_route());
  ASSERT_TRUE(decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(decision.disabled_by_circuit_breaker);

  // Phase 3: After circuit-breaker trip, build using a route that would
  // normally enable ECH but now the breaker overrides. The resulting
  // ClientHello must still be a valid TLS record, not a malformed probe.
  // We use the unknown route (ECH disabled by default) to simulate what
  // the post-transition wire looks like.
  auto wire_ech_off = build_default_tls_client_hello("www.google.com", secret, kBaseTime + 3, unknown_route());
  ASSERT_TRUE(is_valid_tls_client_hello(wire_ech_off));

  auto parsed_ech_off = parse_tls_client_hello(wire_ech_off);
  ASSERT_TRUE(parsed_ech_off.is_ok());
  ASSERT_FALSE(has_ech_extension(parsed_ech_off.ok()));

  // The no-ECH ClientHello must still contain the core set of extensions
  // that any browser would emit (SNI, supported_versions, key_share, etc.).
  ASSERT_TRUE(find_extension(parsed_ech_off.ok(), 0x0000) != nullptr);   // SNI
  ASSERT_TRUE(find_extension(parsed_ech_off.ok(), 0x002B) != nullptr);   // supported_versions
  ASSERT_TRUE(find_extension(parsed_ech_off.ok(), 0x0033) != nullptr);   // key_share
  ASSERT_TRUE(find_extension(parsed_ech_off.ok(), 0x000D) != nullptr);   // signature_algorithms
}

// ---------------------------------------------------------------------------
// TEST 3: Fallback from blocked transport produces a standard ClientHello
//         for the fallback profile.
// ---------------------------------------------------------------------------

TEST(TlsRouteTransitionIndistinguishability,
     FallbackFromBlockedTransportProducesStandardClientHello) {
  td::Slice secret("0123456789secret");
  constexpr int32 kBaseTime = 1712345678;

  // Simulate fallback: first build with ECH (non-RU), then fallback to
  // no-ECH (unknown route, mimicking a transport block that forces a
  // different route). Both must produce valid ClientHellos.

  // "Primary" path -- non-RU with ECH.
  auto wire_primary = build_default_tls_client_hello("www.google.com", secret, kBaseTime, non_ru_route());
  ASSERT_TRUE(is_valid_tls_client_hello(wire_primary));

  // "Fallback" path -- unknown route, ECH disabled.
  auto wire_fallback = build_default_tls_client_hello("www.google.com", secret, kBaseTime + 1, unknown_route());
  ASSERT_TRUE(is_valid_tls_client_hello(wire_fallback));

  auto parsed_fallback = parse_tls_client_hello(wire_fallback);
  ASSERT_TRUE(parsed_fallback.is_ok());

  // Fallback must not carry ECH.
  ASSERT_FALSE(has_ech_extension(parsed_fallback.ok()));

  // Fallback must carry the same core extension set as any steady-state
  // no-ECH build (verify by building a reference no-ECH ClientHello).
  auto wire_ref = build_default_tls_client_hello("www.google.com", secret, kBaseTime + 2, unknown_route());
  auto parsed_ref = parse_tls_client_hello(wire_ref);
  ASSERT_TRUE(parsed_ref.is_ok());

  auto fallback_ext = structural_extension_set(parsed_fallback.ok());
  auto ref_ext = structural_extension_set(parsed_ref.ok());
  ASSERT_TRUE(fallback_ext == ref_ext);
}

// ---------------------------------------------------------------------------
// TEST 4: Wire bytes before and after transition are both valid TLS
//         ClientHellos with no unique markers.
// ---------------------------------------------------------------------------

TEST(TlsRouteTransitionIndistinguishability,
     WireBytesBeforeAndAfterTransitionAreValidClientHellosWithNoUniqueMarkers) {
  td::Slice secret("0123456789secret");
  constexpr int32 kBaseTime = 1712345678;
  constexpr int kIterations = 128;

  // Collect all unique extension-type sets observed across non-RU, RU,
  // and unknown routes. No route should produce an extension type that
  // is never seen in any other route (that would be a unique marker).

  std::unordered_set<uint16> all_non_ru_ext_types;
  std::unordered_set<uint16> all_ru_ext_types;
  std::unordered_set<uint16> all_unknown_ext_types;

  for (int i = 0; i < kIterations; i++) {
    {
      auto wire = build_default_tls_client_hello("www.google.com", secret, kBaseTime + i, non_ru_route());
      ASSERT_TRUE(is_valid_tls_client_hello(wire));
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
      for (const auto &ext : parsed.ok().extensions) {
        if (!is_grease(ext.type)) {
          all_non_ru_ext_types.insert(ext.type);
        }
      }
    }
    {
      auto wire = build_default_tls_client_hello("www.google.com", secret, kBaseTime + 1000 + i, ru_route());
      ASSERT_TRUE(is_valid_tls_client_hello(wire));
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
      for (const auto &ext : parsed.ok().extensions) {
        if (!is_grease(ext.type)) {
          all_ru_ext_types.insert(ext.type);
        }
      }
    }
    {
      auto wire = build_default_tls_client_hello("www.google.com", secret, kBaseTime + 2000 + i, unknown_route());
      ASSERT_TRUE(is_valid_tls_client_hello(wire));
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
      for (const auto &ext : parsed.ok().extensions) {
        if (!is_grease(ext.type)) {
          all_unknown_ext_types.insert(ext.type);
        }
      }
    }
  }

  // The RU and unknown routes must share the same extension-type set (both
  // have ECH disabled).  The non-RU route may add the ECH extension but
  // must not carry any extension type that is completely absent from the
  // no-ECH routes apart from ECH itself.
  for (auto ext_type : all_non_ru_ext_types) {
    if (ext_type == td::mtproto::test::fixtures::kEchExtensionType) {
      continue;  // ECH is expected only in non-RU
    }
    // Every other extension type in non-RU must also appear in the no-ECH
    // routes -- otherwise it is a transition-unique marker.
    bool in_ru = all_ru_ext_types.count(ext_type) > 0;
    bool in_unknown = all_unknown_ext_types.count(ext_type) > 0;
    ASSERT_TRUE(in_ru || in_unknown);
  }

  // Conversely, no-ECH routes must not carry ECH.
  ASSERT_TRUE(all_ru_ext_types.count(td::mtproto::test::fixtures::kEchExtensionType) == 0);
  ASSERT_TRUE(all_unknown_ext_types.count(td::mtproto::test::fixtures::kEchExtensionType) == 0);
}

// ---------------------------------------------------------------------------
// TEST 5: No timing-based distinguisher between normal and transition
//         ClientHellos.
//
// Strategy: Measure the wall-clock cost of building ClientHellos under
// steady-state vs. transition conditions. A statistically significant
// difference would indicate a timing side-channel. Because unit-test
// timing measurements are inherently noisy, we use a generous tolerance
// (4x ratio) and run enough iterations to amortize jitter.
// ---------------------------------------------------------------------------

TEST(TlsRouteTransitionIndistinguishability,
     NoTimingDistinguisherBetweenNormalAndTransitionClientHellos) {
  td::Slice secret("0123456789secret");
  constexpr int32 kBaseTime = 1712345678;
  constexpr int kWarmup = 32;
  constexpr int kMeasure = 256;

  // Warm up the code path to defeat cold-cache effects.
  for (int i = 0; i < kWarmup; i++) {
    (void)build_default_tls_client_hello("www.google.com", secret, kBaseTime + i, non_ru_route());
    (void)build_default_tls_client_hello("www.google.com", secret, kBaseTime + i, ru_route());
  }

  // Measure steady-state (non-RU, ECH enabled).
  auto t0_steady = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < kMeasure; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, kBaseTime + 1000 + i, non_ru_route());
    ASSERT_TRUE(wire.size() > 0);
  }
  auto t1_steady = std::chrono::high_resolution_clock::now();
  auto steady_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1_steady - t0_steady).count();

  // Measure "transition" path: alternate between non-RU and RU on every
  // iteration, simulating rapid route flapping.
  auto t0_transition = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < kMeasure; i++) {
    auto route = (i % 2 == 0) ? non_ru_route() : ru_route();
    auto wire = build_default_tls_client_hello("www.google.com", secret, kBaseTime + 2000 + i, route);
    ASSERT_TRUE(wire.size() > 0);
  }
  auto t1_transition = std::chrono::high_resolution_clock::now();
  auto transition_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1_transition - t0_transition).count();

  // Neither path should be more than 4x slower than the other. A real
  // timing side-channel (e.g. an extra network roundtrip, a sleep, or a
  // heavyweight key derivation triggered only on transitions) would blow
  // past this threshold.
  if (steady_ns > 0 && transition_ns > 0) {
    auto ratio = static_cast<double>(std::max(steady_ns, transition_ns)) /
                 static_cast<double>(std::min(steady_ns, transition_ns));
    ASSERT_TRUE(ratio < 4.0);
  }
}

// ---------------------------------------------------------------------------
// TEST 6 (supplementary): Per-profile ECH-mode transition produces valid
// wire for every known profile.
// ---------------------------------------------------------------------------

TEST(TlsRouteTransitionIndistinguishability,
     PerProfileEchModeTransitionProducesValidWire) {
  td::Slice secret("0123456789secret");
  constexpr int32 kBaseTime = 1712345678;

  // For each profile, build with ECH enabled then with ECH disabled and
  // verify both are valid TLS ClientHellos.
  const BrowserProfile profiles[] = {
      BrowserProfile::Chrome133,
      BrowserProfile::Chrome131,
      BrowserProfile::Chrome120,
      BrowserProfile::Chrome147_Windows,
      BrowserProfile::Firefox148,
      BrowserProfile::Firefox149_Windows,
      BrowserProfile::Safari26_3,
  };

  for (auto profile : profiles) {
    // ECH-enabled build.
    auto wire_ech = build_tls_client_hello_for_profile("www.google.com", secret, kBaseTime, profile,
                                                       EchMode::Rfc9180Outer);
    ASSERT_TRUE(is_valid_tls_client_hello(wire_ech));
    auto parsed_ech = parse_tls_client_hello(wire_ech);
    ASSERT_TRUE(parsed_ech.is_ok());
    ASSERT_TRUE(has_ech_extension(parsed_ech.ok()));

    // ECH-disabled build (simulating post-transition).
    auto wire_no_ech = build_tls_client_hello_for_profile("www.google.com", secret, kBaseTime + 1, profile,
                                                          EchMode::Disabled);
    ASSERT_TRUE(is_valid_tls_client_hello(wire_no_ech));
    auto parsed_no_ech = parse_tls_client_hello(wire_no_ech);
    ASSERT_TRUE(parsed_no_ech.is_ok());
    ASSERT_FALSE(has_ech_extension(parsed_no_ech.ok()));

    // Both must share the same core extension types (minus ECH and padding).
    auto ext_ech = structural_extension_set(parsed_ech.ok());
    auto ext_no_ech = structural_extension_set(parsed_no_ech.ok());

    // Remove ECH from the ECH-enabled set for comparison.
    ext_ech.erase(std::remove(ext_ech.begin(), ext_ech.end(),
                              td::mtproto::test::fixtures::kEchExtensionType),
                  ext_ech.end());

    ASSERT_TRUE(ext_ech == ext_no_ech);
  }
}

// ---------------------------------------------------------------------------
// TEST 7 (supplementary): Rapid route flapping does not accumulate state
// that makes later ClientHellos distinguishable from fresh ones.
// ---------------------------------------------------------------------------

TEST(TlsRouteTransitionIndistinguishability,
     RapidRouteFlappingDoesNotAccumulateDistinguishingState) {
  td::Slice secret("0123456789secret");
  constexpr int32 kBaseTime = 1712345678;
  constexpr int kFlaps = 64;

  // Collect wire lengths from a "fresh" sequence (all non-RU).
  std::unordered_set<size_t> fresh_lengths;
  for (int i = 0; i < kFlaps; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, kBaseTime + i, non_ru_route());
    fresh_lengths.insert(wire.size());
  }

  // Now flap: alternate non-RU / RU / unknown rapidly, then build a final
  // non-RU ClientHello. Its length must fall within the set observed for
  // fresh non-RU builds.
  for (int i = 0; i < kFlaps; i++) {
    NetworkRouteHints route;
    if (i % 3 == 0) {
      route = ru_route();
    } else if (i % 3 == 1) {
      route = unknown_route();
    } else {
      route = non_ru_route();
    }
    (void)build_default_tls_client_hello("www.google.com", secret, kBaseTime + 500 + i, route);
  }

  // Post-flap build must look like a fresh non-RU build.
  auto wire_post_flap = build_default_tls_client_hello("www.google.com", secret, kBaseTime + 1000, non_ru_route());
  ASSERT_TRUE(is_valid_tls_client_hello(wire_post_flap));

  auto parsed_post_flap = parse_tls_client_hello(wire_post_flap);
  ASSERT_TRUE(parsed_post_flap.is_ok());
  ASSERT_TRUE(has_ech_extension(parsed_post_flap.ok()));

  // The post-flap wire length must overlap with the fresh set.
  ASSERT_TRUE(fresh_lengths.count(wire_post_flap.size()) > 0);
}

// ---------------------------------------------------------------------------
// TEST 8 (supplementary): MockRng-driven transition builds are
// deterministic and do not embed route-transition markers.
// ---------------------------------------------------------------------------

TEST(TlsRouteTransitionIndistinguishability,
     DeterministicRngTransitionBuildsMatchSteadyState) {
  td::Slice secret("0123456789secret");
  constexpr int32 kBaseTime = 1712345678;

  // Two independent MockRng instances with the same seed must produce
  // identical wire bytes for the same route, regardless of what the
  // "previous" route was. This proves the builder is stateless with
  // respect to route history.

  MockRng rng1(42);
  auto wire_a = build_default_tls_client_hello("www.google.com", secret, kBaseTime, ru_route(), rng1);

  MockRng rng2(42);
  // "Previous" call was non-RU (simulated).
  (void)build_default_tls_client_hello("www.google.com", secret, kBaseTime - 1, non_ru_route(), rng2);
  // Now build with the same RU route and a fresh RNG with the same seed.
  MockRng rng3(42);
  auto wire_b = build_default_tls_client_hello("www.google.com", secret, kBaseTime, ru_route(), rng3);

  // wire_a and wire_b must be byte-identical because the builder should
  // not carry state from the previous call's route.
  ASSERT_EQ(wire_a.size(), wire_b.size());
  ASSERT_TRUE(wire_a == wire_b);
}

}  // namespace
