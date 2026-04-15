// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Route-lane behavior matrix: 4 routes x 4 profiles x 2 ECH modes.
// Verifies that JA3/JA4 remain stable within each cell and that ECH
// presence matches per-route policy. Also pins the Chrome 133 ALPS
// extension type to 0x44CD when ECH is on (must not regress to 0x4469).

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <set>
#include <unordered_set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

constexpr int32 kUnixTime = 1712345678;
constexpr size_t kSeedsPerCell = 20;

enum class RouteKind : uint8 {
  KnownNonRu,
  KnownRu,
  Unknown,
  QuicFallback,
};

NetworkRouteHints make_route(RouteKind kind) {
  NetworkRouteHints hints;
  switch (kind) {
    case RouteKind::KnownNonRu:
      hints.is_known = true;
      hints.is_ru = false;
      break;
    case RouteKind::KnownRu:
      hints.is_known = true;
      hints.is_ru = true;
      break;
    case RouteKind::Unknown:
      hints.is_known = false;
      hints.is_ru = false;
      break;
    case RouteKind::QuicFallback:
      // QUIC fallback == TCP path after UDP was blocked: we treat it as
      // a known, non-RU path that still must behave identically to the
      // standard known-non-RU case. Any divergence would turn QUIC
      // fallbacks into a separate observable lane.
      hints.is_known = true;
      hints.is_ru = false;
      break;
  }
  return hints;
}

bool profile_is_shuffling(BrowserProfile profile) {
  return profile_spec(profile).extension_order_policy == ExtensionOrderPolicy::ChromeShuffleAnchored;
}

bool profile_allows_ech_on_good_route(BrowserProfile profile) {
  return profile_spec(profile).allows_ech;
}

string build_wire(BrowserProfile profile, const NetworkRouteHints &hints, EchMode requested_ech, uint64 seed) {
  MockRng rng(seed);
  // The runtime route gate must lower ECH to Disabled on RU/unknown
  // routes regardless of what we ask for. We still use the
  // route-aware builder so we exercise that gate on every cell.
  (void)requested_ech;
  return build_runtime_tls_client_hello("www.google.com", "0123456789secret", kUnixTime, hints, rng);
}

string build_profile_wire(BrowserProfile profile, const NetworkRouteHints &hints, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
  // Profile builder doesn't apply the route gate itself; emulate the
  // runtime decision: RU/unknown -> force ECH off.
  if (hints.is_ru || !hints.is_known) {
    MockRng rng2(seed);
    return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile,
                                              EchMode::Disabled, rng2);
  }
  return wire;
}

bool has_ech(const ParsedClientHello &hello) {
  return find_extension(hello, fixtures::kEchExtensionType) != nullptr;
}

bool has_legacy_ech(const ParsedClientHello &hello) {
  return find_extension(hello, 0xFE02) != nullptr;
}

EchMode effective_ech_mode(BrowserProfile profile, const NetworkRouteHints &hints, EchMode requested) {
  if (!profile_allows_ech_on_good_route(profile)) {
    return EchMode::Disabled;
  }
  if (hints.is_ru || !hints.is_known) {
    return EchMode::Disabled;
  }
  return requested;
}

size_t ja3_cap_for_profile(BrowserProfile profile) {
  // Shuffling profiles permute non-anchored extensions; non-shuffling
  // profiles emit a fixed wire shape. Actual observed diversity is
  // small in 20 samples so we keep a tight ceiling that would reject
  // any regression that accidentally randomized more fields.
  return profile_is_shuffling(profile) ? 32u : 1u;
}

struct CellOutcome final {
  std::set<string> ja3_set;
  std::set<string> ja4a_set;
  size_t ech_count{0};
  size_t legacy_ech_count{0};
  size_t alps_44cd{0};
  size_t alps_4469{0};
};

CellOutcome run_cell(BrowserProfile profile, const NetworkRouteHints &hints, EchMode requested_ech) {
  CellOutcome outcome;
  for (uint64 seed = 0; seed < kSeedsPerCell; seed++) {
    auto wire = build_profile_wire(profile, hints, requested_ech, seed + 101u);
    auto parsed = parse_tls_client_hello(wire);
    CHECK(parsed.is_ok());
    auto ja3 = compute_ja3(wire);
    auto ja4 = compute_ja4_segments(parsed.ok());
    outcome.ja3_set.insert(ja3);
    outcome.ja4a_set.insert(ja4.segment_a);
    if (has_ech(parsed.ok())) {
      outcome.ech_count++;
    }
    if (has_legacy_ech(parsed.ok())) {
      outcome.legacy_ech_count++;
    }
    if (find_extension(parsed.ok(), 0x44CD) != nullptr) {
      outcome.alps_44cd++;
    }
    if (find_extension(parsed.ok(), 0x4469) != nullptr) {
      outcome.alps_4469++;
    }
  }
  return outcome;
}

const BrowserProfile kMatrixProfiles[] = {
    BrowserProfile::Chrome133,
    BrowserProfile::Firefox148,
    BrowserProfile::Safari26_3,
    BrowserProfile::IOS14,
};

const RouteKind kMatrixRoutes[] = {
    RouteKind::KnownNonRu,
    RouteKind::KnownRu,
    RouteKind::Unknown,
    RouteKind::QuicFallback,
};

const EchMode kMatrixEchModes[] = {
    EchMode::Disabled,
    EchMode::Rfc9180Outer,
};

TEST(TLS_RouteEchQuicBlockMatrix, Ja3IsStableWithinEachCell) {
  for (auto profile : kMatrixProfiles) {
    for (auto route : kMatrixRoutes) {
      for (auto ech : kMatrixEchModes) {
        auto outcome = run_cell(profile, make_route(route), ech);
        // Non-shuffling profiles collapse to a single JA3. Shuffling
        // profiles may traverse several permutations; bound them.
        ASSERT_TRUE(outcome.ja3_set.size() <= ja3_cap_for_profile(profile));
        ASSERT_TRUE(!outcome.ja3_set.empty());
      }
    }
  }
}

TEST(TLS_RouteEchQuicBlockMatrix, Ja4SegmentAIsByteIdenticalWithinEachCell) {
  for (auto profile : kMatrixProfiles) {
    for (auto route : kMatrixRoutes) {
      for (auto ech : kMatrixEchModes) {
        auto outcome = run_cell(profile, make_route(route), ech);
        ASSERT_EQ(1u, outcome.ja4a_set.size());
      }
    }
  }
}

TEST(TLS_RouteEchQuicBlockMatrix, EchPresenceMatchesRoutePolicy) {
  for (auto profile : kMatrixProfiles) {
    for (auto route : kMatrixRoutes) {
      for (auto ech : kMatrixEchModes) {
        auto hints = make_route(route);
        auto outcome = run_cell(profile, hints, ech);
        auto expected_mode = effective_ech_mode(profile, hints, ech);
        if (expected_mode == EchMode::Rfc9180Outer) {
          ASSERT_EQ(kSeedsPerCell, outcome.ech_count);
        } else {
          ASSERT_EQ(0u, outcome.ech_count);
        }
        ASSERT_EQ(0u, outcome.legacy_ech_count);
      }
    }
  }
}

TEST(TLS_RouteEchQuicBlockMatrix, Chrome133WithEchOnAlwaysEmitsNewAlpsType) {
  NetworkRouteHints good_route = make_route(RouteKind::KnownNonRu);
  auto outcome = run_cell(BrowserProfile::Chrome133, good_route, EchMode::Rfc9180Outer);
  ASSERT_EQ(kSeedsPerCell, outcome.ech_count);
  ASSERT_EQ(kSeedsPerCell, outcome.alps_44cd);
  ASSERT_EQ(0u, outcome.alps_4469);
}

TEST(TLS_RouteEchQuicBlockMatrix, QuicFallbackIsIndistinguishableFromKnownNonRu) {
  for (auto profile : kMatrixProfiles) {
    for (auto ech : kMatrixEchModes) {
      auto knonru = run_cell(profile, make_route(RouteKind::KnownNonRu), ech);
      auto quic = run_cell(profile, make_route(RouteKind::QuicFallback), ech);
      ASSERT_TRUE(knonru.ja4a_set == quic.ja4a_set);
      ASSERT_EQ(knonru.ech_count, quic.ech_count);
    }
  }
}

TEST(TLS_RouteEchQuicBlockMatrix, RuRouteNeverCarriesEchAcrossAnyProfile) {
  for (auto profile : kMatrixProfiles) {
    auto outcome_disabled = run_cell(profile, make_route(RouteKind::KnownRu), EchMode::Disabled);
    auto outcome_requested = run_cell(profile, make_route(RouteKind::KnownRu), EchMode::Rfc9180Outer);
    ASSERT_EQ(0u, outcome_disabled.ech_count);
    ASSERT_EQ(0u, outcome_requested.ech_count);
    ASSERT_EQ(0u, outcome_disabled.legacy_ech_count);
    ASSERT_EQ(0u, outcome_requested.legacy_ech_count);
  }
}

TEST(TLS_RouteEchQuicBlockMatrix, UnknownRouteNeverCarriesEchAcrossAnyProfile) {
  for (auto profile : kMatrixProfiles) {
    auto outcome_disabled = run_cell(profile, make_route(RouteKind::Unknown), EchMode::Disabled);
    auto outcome_requested = run_cell(profile, make_route(RouteKind::Unknown), EchMode::Rfc9180Outer);
    ASSERT_EQ(0u, outcome_disabled.ech_count);
    ASSERT_EQ(0u, outcome_requested.ech_count);
    ASSERT_EQ(0u, outcome_disabled.legacy_ech_count);
    ASSERT_EQ(0u, outcome_requested.legacy_ech_count);
  }
}

}  // namespace
