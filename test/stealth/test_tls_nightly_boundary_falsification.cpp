// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Nightly-scale boundary falsification. Asserts four negative
// invariants across a full 10k-seed sweep per profile. Gated on
// TD_NIGHTLY_CORPUS.

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <unordered_set>

namespace {

using namespace td;
using namespace td::mtproto;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

constexpr int32 kUnixTime = 1712345678;
constexpr uint64 kNightlyIterations = kFullIterations * 10;

string build_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
}

// The Chrome-family "allowed" non-GREASE extension set is exactly the
// union of reviewed fixtures; any type outside this set is by
// definition unexpected.
std::unordered_set<uint16> chrome_allowed_extension_set() {
  std::unordered_set<uint16> allowed = kChrome133EchExtensionSet;
  // Chrome131 keeps the legacy ALPS codepoint while Chrome133+ uses 0x44CD.
  allowed.insert(fixtures::kAlpsChrome131);
  // Padding is a Chrome-family extension we permit (dynamic presence).
  allowed.insert(0x0015);
  return allowed;
}

vector<uint16> chrome_reviewed_order_for_profile(BrowserProfile profile) {
  auto reviewed_order = fixtures::reviewed::chrome146_75_linux_desktopNonGreaseExtensionsWithoutPadding;
  if (profile == BrowserProfile::Chrome131) {
    for (auto &ext : reviewed_order) {
      if (ext == fixtures::kAlpsChrome133Plus) {
        ext = fixtures::kAlpsChrome131;
      }
    }
  }
  return reviewed_order;
}

std::unordered_set<uint16> firefox_allowed_extension_set() {
  std::unordered_set<uint16> allowed(kFirefox148ExtensionOrder.begin(), kFirefox148ExtensionOrder.end());
  allowed.insert(0x0015);
  return allowed;
}

std::unordered_set<uint16> safari_allowed_extension_set() {
  std::unordered_set<uint16> allowed = kSafariIosExtensionSet;
  allowed.insert(0x0015);
  return allowed;
}

// Assert no forbidden extension type appears. "Forbidden" = any non-GREASE
// extension type not present in the reviewed set for this family.
void assert_no_forbidden_extensions(const ParsedClientHello &hello, const std::unordered_set<uint16> &allowed) {
  for (const auto &ext : hello.extensions) {
    if (is_grease_value(ext.type)) {
      continue;
    }
    ASSERT_TRUE(allowed.count(ext.type) == 1u);
  }
}

// Assert that the order of non-GREASE, non-padding extensions is a
// permutation of the declared reviewed order. For Firefox the reviewed
// order is exact; for Chrome the reviewed order is a declared shuffle
// set (as a multiset, the non-GREASE extension identity must match).
void assert_extension_order_is_legal_permutation(const ParsedClientHello &hello, const vector<uint16> &reviewed_order,
                                                 bool require_exact) {
  auto observed = non_grease_extension_sequence(hello);
  if (require_exact) {
    ASSERT_EQ(reviewed_order.size(), observed.size());
    for (size_t i = 0; i < reviewed_order.size(); i++) {
      ASSERT_EQ(reviewed_order[i], observed[i]);
    }
    return;
  }
  // Permutation check: same multiset of extension types.
  auto a = reviewed_order;
  auto b = observed;
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());
  ASSERT_EQ(a, b);
}

// Assert the key-share total length is exactly 0x04C0 (1216 bytes) for
// PQ hybrid profiles. "Total" means the concatenated key data across
// all entries containing the PQ hybrid group.
void assert_pq_hybrid_key_share_length(const ParsedClientHello &hello) {
  bool saw_pq = false;
  for (const auto &entry : hello.key_share_entries) {
    if (entry.group == fixtures::kPqHybridGroup || entry.group == fixtures::kPqHybridDraftGroup) {
      saw_pq = true;
      ASSERT_EQ(static_cast<uint16>(fixtures::kPqHybridKeyShareLength), entry.key_length);
    }
  }
  ASSERT_TRUE(saw_pq);
}

// Assert no trailing garbage after the last extension. The parser
// already rejects this on entry; if the wire were generated with
// trailing bytes, `parse_tls_client_hello` would fail. We surface that
// contract explicitly here.
void assert_no_trailing_garbage(Slice wire) {
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
}

void run_boundary_falsification_chrome_family(BrowserProfile profile, EchMode ech_mode) {
  auto allowed = chrome_allowed_extension_set();
  const auto reviewed_order = chrome_reviewed_order_for_profile(profile);
  const auto &spec = profile_spec(profile);

  for (uint64 seed = 0; seed < kNightlyIterations; seed++) {
    auto wire = build_hello(profile, ech_mode, seed);
    assert_no_trailing_garbage(wire);
    auto parsed = parse_tls_client_hello(wire).move_as_ok();

    assert_no_forbidden_extensions(parsed, allowed);
    assert_extension_order_is_legal_permutation(parsed, reviewed_order, /*require_exact=*/false);
    if (spec.has_pq) {
      assert_pq_hybrid_key_share_length(parsed);
    }
  }
}

void run_boundary_falsification_firefox(BrowserProfile profile, EchMode ech_mode) {
  auto allowed = firefox_allowed_extension_set();
  const auto &reviewed_order = fixtures::reviewed::firefox148_linux_desktopNonGreaseExtensionsWithoutPadding;
  const auto &spec = profile_spec(profile);

  for (uint64 seed = 0; seed < kNightlyIterations; seed++) {
    auto wire = build_hello(profile, ech_mode, seed);
    assert_no_trailing_garbage(wire);
    auto parsed = parse_tls_client_hello(wire).move_as_ok();

    assert_no_forbidden_extensions(parsed, allowed);
    assert_extension_order_is_legal_permutation(parsed, reviewed_order, /*require_exact=*/true);
    if (spec.has_pq) {
      assert_pq_hybrid_key_share_length(parsed);
    }
  }
}

void run_boundary_falsification_safari_family(BrowserProfile profile, EchMode ech_mode) {
  auto allowed = safari_allowed_extension_set();
  const auto &spec = profile_spec(profile);

  for (uint64 seed = 0; seed < kNightlyIterations; seed++) {
    auto wire = build_hello(profile, ech_mode, seed);
    assert_no_trailing_garbage(wire);
    auto parsed = parse_tls_client_hello(wire).move_as_ok();

    assert_no_forbidden_extensions(parsed, allowed);
    if (spec.has_pq) {
      assert_pq_hybrid_key_share_length(parsed);
    }
  }
}

TEST(TLS_NightlyBoundaryFalsification, Chrome133EchRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_boundary_falsification_chrome_family(BrowserProfile::Chrome133, EchMode::Rfc9180Outer);
}

TEST(TLS_NightlyBoundaryFalsification, Chrome131EchRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_boundary_falsification_chrome_family(BrowserProfile::Chrome131, EchMode::Rfc9180Outer);
}

TEST(TLS_NightlyBoundaryFalsification, Firefox148EchRun) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_boundary_falsification_firefox(BrowserProfile::Firefox148, EchMode::Rfc9180Outer);
}

TEST(TLS_NightlyBoundaryFalsification, Safari26_3Run) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_boundary_falsification_safari_family(BrowserProfile::Safari26_3, EchMode::Disabled);
}

TEST(TLS_NightlyBoundaryFalsification, IOS14Run) {
  if (!td::mtproto::test::is_nightly_corpus_enabled()) {
    return;
  }
  run_boundary_falsification_safari_family(BrowserProfile::IOS14, EchMode::Disabled);
}

TEST(TLS_NightlyBoundaryFalsification, ChromeFamilyAllowListContainsBothAlpsCodepoints) {
  const auto allowed = chrome_allowed_extension_set();
  ASSERT_TRUE(allowed.count(fixtures::kAlpsChrome131) == 1u);
  ASSERT_TRUE(allowed.count(fixtures::kAlpsChrome133Plus) == 1u);
}

TEST(TLS_NightlyBoundaryFalsification, Chrome131ReviewedOrderUsesLegacyAlpsOnly) {
  const auto chrome131_order = chrome_reviewed_order_for_profile(BrowserProfile::Chrome131);
  ASSERT_TRUE(std::find(chrome131_order.begin(), chrome131_order.end(), fixtures::kAlpsChrome131) !=
              chrome131_order.end());
  ASSERT_TRUE(std::find(chrome131_order.begin(), chrome131_order.end(), fixtures::kAlpsChrome133Plus) ==
              chrome131_order.end());

  const auto chrome133_order = chrome_reviewed_order_for_profile(BrowserProfile::Chrome133);
  ASSERT_TRUE(std::find(chrome133_order.begin(), chrome133_order.end(), fixtures::kAlpsChrome133Plus) !=
              chrome133_order.end());
  ASSERT_TRUE(std::find(chrome133_order.begin(), chrome133_order.end(), fixtures::kAlpsChrome131) ==
              chrome133_order.end());
}

}  // namespace
