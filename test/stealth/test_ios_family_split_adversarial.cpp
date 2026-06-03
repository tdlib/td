// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Adversarial tests proving that iOS 17.2/18.7 Safari (legacy 4-version
// supported_versions [0x0304,0x0303,0x0302,0x0301]) and iOS 26 Safari
// (modern 2-version [0x0304,0x0303]) must not be pooled into a single
// family lane. Pooling these two generations would create a synthetic
// fingerprint that matches neither real generation and is trivially
// distinguishable by a passive observer inspecting the supported_versions
// vector length, key_share structure, or supported_groups composition.

#include "test/stealth/FamilyLaneMatchers.h"

#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>

namespace {

using td::mtproto::test::baselines::ExactInvariants;
using td::mtproto::test::baselines::FamilyLaneBaseline;
using td::mtproto::test::baselines::SetMembershipCatalog;
using td::mtproto::test::baselines::TierLevel;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::ParsedClientHello;
using td::mtproto::test::ParsedExtension;
using td::Slice;
using td::string;
using td::uint16;
using td::uint8;

// ---------------------------------------------------------------------------
// Canonical supported_versions vectors for the two iOS Safari generations.
// ---------------------------------------------------------------------------

// iOS 17.2 / 18.7 Safari: advertises TLS 1.3, 1.2, 1.1, 1.0.
static const td::vector<uint16> kLegacySupportedVersions = {0x0304u, 0x0303u, 0x0302u, 0x0301u};

// iOS 26 Safari: advertises only TLS 1.3 and 1.2.
static const td::vector<uint16> kModernSupportedVersions = {0x0304u, 0x0303u};

// ---------------------------------------------------------------------------
// Helpers to build the supported_versions extension body from a version
// vector. The ClientHello supported_versions body is:
//   [list_length_byte] [version_1 (2B)] ... [version_N (2B)]
// ---------------------------------------------------------------------------

string build_supported_versions_body(const td::vector<uint16> &versions) {
  string body;
  body.push_back(static_cast<char>(static_cast<uint8>(versions.size() * 2)));
  for (auto v : versions) {
    body.push_back(static_cast<char>(static_cast<uint8>(v >> 8)));
    body.push_back(static_cast<char>(static_cast<uint8>(v & 0xFF)));
  }
  return body;
}

td::vector<uint16> parse_supported_versions_from_extension(const ParsedExtension &ext) {
  td::vector<uint16> out;
  if (ext.value.empty()) {
    return out;
  }
  const auto versions_len = static_cast<uint8>(ext.value[0]);
  for (size_t i = 1; i + 1 < ext.value.size() && i < static_cast<size_t>(versions_len + 1); i += 2) {
    auto version = static_cast<uint16>((static_cast<uint8>(ext.value[i]) << 8) |
                                       static_cast<uint8>(ext.value[i + 1]));
    out.push_back(version);
  }
  return out;
}

// ---------------------------------------------------------------------------
// Synthetic baselines for the two iOS Safari generations.
// ---------------------------------------------------------------------------

// Legacy iOS (17.2 / 18.7): no PQ hybrid, 4 supported groups without
// 0x11EC, cipher suites start with 0x1301, supported_versions has 4 entries.
FamilyLaneBaseline make_legacy_ios_baseline() {
  FamilyLaneBaseline b{};
  b.family_id = Slice("apple_ios_tls_legacy");
  b.route_lane = Slice("non_ru_egress");
  b.tier = TierLevel::Tier2;
  b.raw_tier = TierLevel::Tier2;
  b.sample_count = 4;
  b.authoritative_sample_count = 4;
  b.num_sources = 2;
  b.num_sessions = 4;
  b.stale_over_90_days = false;
  b.stale_over_180_days = false;
  b.invariants.family_id = b.family_id;
  b.invariants.route_lane = b.route_lane;
  // Safari 17.2 / 18.7 cipher suite order (GREASE stripped).
  b.invariants.non_grease_cipher_suites_ordered = {
      0x1301u, 0x1302u, 0x1303u, 0xC02Cu, 0xC02Bu, 0xCCA9u, 0xC030u,
      0xC02Fu, 0xCCA8u, 0xC00Au, 0xC009u, 0xC014u, 0xC013u, 0x009Du,
      0x009Cu, 0x0035u, 0x002Fu, 0xC008u, 0xC012u, 0x000Au};
  // Legacy: no PQ hybrid group.
  b.invariants.non_grease_supported_groups = {0x001Du, 0x0017u, 0x0018u, 0x0019u};
  b.invariants.compress_cert_algorithms = {0x0001u};
  b.invariants.non_grease_extension_set = {
      0x0000u, 0x0017u, 0xFF01u, 0x000Au, 0x000Bu, 0x0010u,
      0x0005u, 0x000Du, 0x0012u, 0x0033u, 0x002Du, 0x002Bu, 0x001Bu};
  b.invariants.tls_record_version = 0x0301u;
  b.invariants.client_hello_legacy_version = 0x0303u;
  b.set_catalog.observed_wire_lengths = {512u};
  return b;
}

// Modern iOS (26.x): PQ hybrid 0x11EC present, cipher suites start with
// 0x1302, supported_versions has only 2 entries.
FamilyLaneBaseline make_modern_ios_baseline() {
  FamilyLaneBaseline b{};
  b.family_id = Slice("apple_ios_tls_modern");
  b.route_lane = Slice("non_ru_egress");
  b.tier = TierLevel::Tier2;
  b.raw_tier = TierLevel::Tier2;
  b.sample_count = 4;
  b.authoritative_sample_count = 4;
  b.num_sources = 2;
  b.num_sessions = 4;
  b.stale_over_90_days = false;
  b.stale_over_180_days = false;
  b.invariants.family_id = b.family_id;
  b.invariants.route_lane = b.route_lane;
  // Safari 26.x cipher suite order (GREASE stripped): note 0x1302 leads.
  b.invariants.non_grease_cipher_suites_ordered = {
      0x1302u, 0x1303u, 0x1301u, 0xC02Cu, 0xC02Bu, 0xCCA9u, 0xC030u,
      0xC02Fu, 0xCCA8u, 0xC00Au, 0xC009u, 0xC014u, 0xC013u, 0x009Du,
      0x009Cu, 0x0035u, 0x002Fu, 0xC008u, 0xC012u, 0x000Au};
  // Modern: PQ hybrid group 0x11EC present.
  b.invariants.non_grease_supported_groups = {0x11ECu, 0x001Du, 0x0017u, 0x0018u, 0x0019u};
  b.invariants.compress_cert_algorithms = {0x0001u};
  b.invariants.non_grease_extension_set = {
      0x0000u, 0x0017u, 0xFF01u, 0x000Au, 0x000Bu, 0x0010u,
      0x0005u, 0x000Du, 0x0012u, 0x0033u, 0x002Du, 0x002Bu, 0x001Bu};
  b.invariants.tls_record_version = 0x0301u;
  b.invariants.client_hello_legacy_version = 0x0303u;
  b.set_catalog.observed_wire_lengths = {1540u, 1543u};
  return b;
}

// ---------------------------------------------------------------------------
// Fabricate a minimal ParsedClientHello that looks like legacy iOS Safari.
// Only the fields relevant to invariant matching are populated.
// ---------------------------------------------------------------------------

ParsedClientHello make_legacy_ios_hello() {
  ParsedClientHello hello;
  hello.owned_wire = std::make_unique<string>();

  hello.record_legacy_version = 0x0301u;
  hello.client_legacy_version = 0x0303u;

  // Legacy cipher suites with leading GREASE.
  static const uint8 kLegacyCipherSuitesRaw[] = {
      // Length prefix (2 bytes): 21 suites * 2 = 42 bytes.
      0x00, 0x2A,
      // GREASE
      0xDA, 0xDA,
      // Non-GREASE suites (Safari 17.2 order).
      0x13, 0x01, 0x13, 0x02, 0x13, 0x03, 0xC0, 0x2C, 0xC0, 0x2B,
      0xCC, 0xA9, 0xC0, 0x30, 0xC0, 0x2F, 0xCC, 0xA8, 0xC0, 0x0A,
      0xC0, 0x09, 0xC0, 0x14, 0xC0, 0x13, 0x00, 0x9D, 0x00, 0x9C,
      0x00, 0x35, 0x00, 0x2F, 0xC0, 0x08, 0xC0, 0x12, 0x00, 0x0A};
  hello.cipher_suites = Slice(reinterpret_cast<const char *>(kLegacyCipherSuitesRaw + 2), 42);

  // Legacy supported groups: no PQ hybrid.
  hello.supported_groups = {0x3A3Au, 0x001Du, 0x0017u, 0x0018u, 0x0019u};

  // Key share: GREASE + X25519 only.
  hello.key_share_entries.push_back({0x3A3Au, 1});
  hello.key_share_entries.push_back({0x001Du, 32});

  // Build extensions list.
  td::vector<uint16> ext_types = {0x9A9Au, 0x0000u, 0x0017u, 0xFF01u, 0x000Au,
                                  0x000Bu, 0x0010u, 0x0005u, 0x000Du, 0x0012u,
                                  0x0033u, 0x002Du, 0x002Bu, 0x001Bu, 0x6A6Au,
                                  0x0015u};
  for (auto t : ext_types) {
    ParsedExtension ext;
    ext.type = t;
    if (t == 0x002Bu) {
      // supported_versions: 4-version legacy vector.
      *hello.owned_wire += build_supported_versions_body(kLegacySupportedVersions);
      ext.value = Slice(hello.owned_wire->data() + hello.owned_wire->size() -
                            (kLegacySupportedVersions.size() * 2 + 1),
                        kLegacySupportedVersions.size() * 2 + 1);
    } else if (t == 0x001Bu) {
      // compress_certificate: zlib = 0x0001.
      static const char kCompressBody[] = "\x02\x00\x01";
      ext.value = Slice(kCompressBody, 3);
    }
    hello.extensions.push_back(ext);
  }
  return hello;
}

// ---------------------------------------------------------------------------
// Fabricate a minimal ParsedClientHello that looks like modern iOS 26 Safari.
// ---------------------------------------------------------------------------

ParsedClientHello make_modern_ios_hello() {
  ParsedClientHello hello;
  hello.owned_wire = std::make_unique<string>();

  hello.record_legacy_version = 0x0301u;
  hello.client_legacy_version = 0x0303u;

  // Modern cipher suites: note 0x1302 leads (not 0x1301).
  static const uint8 kModernCipherSuitesRaw[] = {
      // Length prefix: 21 suites * 2 = 42 bytes.
      0x00, 0x2A,
      // GREASE
      0x1A, 0x1A,
      // Non-GREASE suites (Safari 26.x order).
      0x13, 0x02, 0x13, 0x03, 0x13, 0x01, 0xC0, 0x2C, 0xC0, 0x2B,
      0xCC, 0xA9, 0xC0, 0x30, 0xC0, 0x2F, 0xCC, 0xA8, 0xC0, 0x0A,
      0xC0, 0x09, 0xC0, 0x14, 0xC0, 0x13, 0x00, 0x9D, 0x00, 0x9C,
      0x00, 0x35, 0x00, 0x2F, 0xC0, 0x08, 0xC0, 0x12, 0x00, 0x0A};
  hello.cipher_suites = Slice(reinterpret_cast<const char *>(kModernCipherSuitesRaw + 2), 42);

  // Modern supported groups: PQ hybrid 0x11EC present.
  hello.supported_groups = {0x1A1Au, 0x11ECu, 0x001Du, 0x0017u, 0x0018u, 0x0019u};

  // Key share: GREASE + PQ hybrid + X25519.
  hello.key_share_entries.push_back({0x1A1Au, 1});
  hello.key_share_entries.push_back({0x11ECu, 1216});
  hello.key_share_entries.push_back({0x001Du, 32});

  // Build extensions list (no padding extension in modern Safari).
  td::vector<uint16> ext_types = {0x6A6Au, 0x0000u, 0x0017u, 0xFF01u, 0x000Au,
                                  0x000Bu, 0x0010u, 0x0005u, 0x000Du, 0x0012u,
                                  0x0033u, 0x002Du, 0x002Bu, 0x001Bu, 0x5A5Au};
  for (auto t : ext_types) {
    ParsedExtension ext;
    ext.type = t;
    if (t == 0x002Bu) {
      // supported_versions: 2-version modern vector.
      *hello.owned_wire += build_supported_versions_body(kModernSupportedVersions);
      ext.value = Slice(hello.owned_wire->data() + hello.owned_wire->size() -
                            (kModernSupportedVersions.size() * 2 + 1),
                        kModernSupportedVersions.size() * 2 + 1);
    } else if (t == 0x001Bu) {
      // compress_certificate: zlib = 0x0001.
      static const char kCompressBody[] = "\x02\x00\x01";
      ext.value = Slice(kCompressBody, 3);
    }
    hello.extensions.push_back(ext);
  }
  return hello;
}

// ===========================================================================
// TEST 1: Legacy vector must fail the modern matcher's cipher suite order.
//
// Safari 17.2/18.7 starts cipher suites with 0x1301 while Safari 26 starts
// with 0x1302. A modern baseline that pins the 0x1302-leading order must
// reject a legacy hello whose order starts with 0x1301.
// ===========================================================================

TEST(IosFamilySplitAdversarial, LegacyVectorFailsModernMatcherCipherSuiteOrder) {
  auto modern_baseline = make_modern_ios_baseline();
  FamilyLaneMatcher modern_matcher(modern_baseline);
  auto legacy_hello = make_legacy_ios_hello();
  ASSERT_FALSE(modern_matcher.matches_exact_invariants(legacy_hello));
}

// ===========================================================================
// TEST 2: Modern vector must fail the legacy matcher's cipher suite order.
//
// The inverse: Safari 26 hello starts with 0x1302 but the legacy baseline
// pins 0x1301-first. The matcher must reject.
// ===========================================================================

TEST(IosFamilySplitAdversarial, ModernVectorFailsLegacyMatcherCipherSuiteOrder) {
  auto legacy_baseline = make_legacy_ios_baseline();
  FamilyLaneMatcher legacy_matcher(legacy_baseline);
  auto modern_hello = make_modern_ios_hello();
  ASSERT_FALSE(legacy_matcher.matches_exact_invariants(modern_hello));
}

// ===========================================================================
// TEST 3: Legacy vector must fail the modern matcher's supported groups.
//
// Safari 26 includes PQ hybrid group 0x11EC; Safari 17.2/18.7 does not.
// A modern baseline pinning 0x11EC in supported_groups must reject a legacy
// hello that lacks it.
// ===========================================================================

TEST(IosFamilySplitAdversarial, LegacyVectorFailsModernMatcherSupportedGroups) {
  auto modern_baseline = make_modern_ios_baseline();
  // Clear cipher suites to isolate the supported_groups check.
  modern_baseline.invariants.non_grease_cipher_suites_ordered.clear();
  FamilyLaneMatcher modern_matcher(modern_baseline);
  auto legacy_hello = make_legacy_ios_hello();
  ASSERT_FALSE(modern_matcher.matches_exact_invariants(legacy_hello));
}

// ===========================================================================
// TEST 4: Modern vector must fail the legacy matcher's supported groups.
//
// Inverse of test 3: the legacy baseline pins {X25519, P256, P384, P521}
// without 0x11EC. A modern hello carrying {0x11EC, X25519, P256, P384, P521}
// must be rejected because the group vector differs.
// ===========================================================================

TEST(IosFamilySplitAdversarial, ModernVectorFailsLegacyMatcherSupportedGroups) {
  auto legacy_baseline = make_legacy_ios_baseline();
  // Clear cipher suites to isolate supported_groups.
  legacy_baseline.invariants.non_grease_cipher_suites_ordered.clear();
  FamilyLaneMatcher legacy_matcher(legacy_baseline);
  auto modern_hello = make_modern_ios_hello();
  ASSERT_FALSE(legacy_matcher.matches_exact_invariants(modern_hello));
}

// ===========================================================================
// TEST 5: Mixing generations into a single baseline empties invariants.
//
// If someone pooled both generations into one FamilyLaneBaseline, the only
// sound strategy would be to leave cipher_suites and supported_groups empty
// (since they differ between the two generations). An empty baseline must
// then vacuously accept *both* generation hellos -- which means the
// invariant check becomes useless (matches anything). Prove this degenerate
// state by showing a baseline with all invariants cleared accepts both.
// ===========================================================================

TEST(IosFamilySplitAdversarial, PooledEmptyInvariantsAcceptBothGenerationsVacuously) {
  FamilyLaneBaseline pooled{};
  pooled.family_id = Slice("apple_ios_tls");
  pooled.route_lane = Slice("non_ru_egress");
  pooled.tier = TierLevel::Tier2;
  pooled.raw_tier = TierLevel::Tier2;
  pooled.sample_count = 8;
  pooled.authoritative_sample_count = 8;
  pooled.num_sources = 4;
  pooled.num_sessions = 8;
  // All invariant vectors left empty -- simulating a pooled baseline where
  // the two generations disagree on every exact field.
  pooled.invariants.family_id = pooled.family_id;
  pooled.invariants.route_lane = pooled.route_lane;
  pooled.invariants.tls_record_version = 0x0301u;
  pooled.invariants.client_hello_legacy_version = 0x0303u;

  FamilyLaneMatcher pooled_matcher(pooled);

  // Both generations pass vacuously -- the matcher cannot distinguish them.
  ASSERT_TRUE(pooled_matcher.matches_exact_invariants(make_legacy_ios_hello()));
  ASSERT_TRUE(pooled_matcher.matches_exact_invariants(make_modern_ios_hello()));
}

// ===========================================================================
// TEST 6: Pooled baseline with only extension_set still cannot split.
//
// Both generations share the same 13-element non-GREASE extension set, so
// pinning extension_set alone fails to distinguish them. Verify that both
// pass when only the extension_set invariant is populated.
// ===========================================================================

TEST(IosFamilySplitAdversarial, SharedExtensionSetCannotSplitGenerations) {
  FamilyLaneBaseline shared{};
  shared.family_id = Slice("apple_ios_tls");
  shared.route_lane = Slice("non_ru_egress");
  shared.tier = TierLevel::Tier2;
  shared.raw_tier = TierLevel::Tier2;
  shared.sample_count = 8;
  shared.authoritative_sample_count = 8;
  shared.num_sources = 4;
  shared.num_sessions = 8;
  shared.invariants.family_id = shared.family_id;
  shared.invariants.route_lane = shared.route_lane;
  // Both generations share this exact extension set.
  shared.invariants.non_grease_extension_set = {
      0x0000u, 0x0017u, 0xFF01u, 0x000Au, 0x000Bu, 0x0010u,
      0x0005u, 0x000Du, 0x0012u, 0x0033u, 0x002Du, 0x002Bu, 0x001Bu};
  shared.invariants.tls_record_version = 0x0301u;
  shared.invariants.client_hello_legacy_version = 0x0303u;

  FamilyLaneMatcher shared_matcher(shared);

  // Neither is rejected -- extension_set alone is insufficient for split.
  ASSERT_TRUE(shared_matcher.matches_exact_invariants(make_legacy_ios_hello()));
  ASSERT_TRUE(shared_matcher.matches_exact_invariants(make_modern_ios_hello()));
}

// ===========================================================================
// TEST 7: Generation-aware cohort split works via supported_groups alone.
//
// Even without cipher suite pinning, supported_groups is sufficient to
// distinguish the two generations. Verify that a legacy baseline rejects
// modern and vice versa when only supported_groups is populated.
// ===========================================================================

TEST(IosFamilySplitAdversarial, GenerationAwareCohortSplitViaSupportedGroupsAlone) {
  // Legacy baseline with only supported_groups.
  FamilyLaneBaseline legacy_groups_only{};
  legacy_groups_only.family_id = Slice("apple_ios_tls_legacy");
  legacy_groups_only.route_lane = Slice("non_ru_egress");
  legacy_groups_only.tier = TierLevel::Tier2;
  legacy_groups_only.raw_tier = TierLevel::Tier2;
  legacy_groups_only.sample_count = 2;
  legacy_groups_only.authoritative_sample_count = 2;
  legacy_groups_only.num_sources = 1;
  legacy_groups_only.num_sessions = 2;
  legacy_groups_only.invariants.family_id = legacy_groups_only.family_id;
  legacy_groups_only.invariants.route_lane = legacy_groups_only.route_lane;
  legacy_groups_only.invariants.non_grease_supported_groups = {0x001Du, 0x0017u, 0x0018u, 0x0019u};
  legacy_groups_only.invariants.tls_record_version = 0x0301u;
  legacy_groups_only.invariants.client_hello_legacy_version = 0x0303u;

  FamilyLaneMatcher legacy_only(legacy_groups_only);
  ASSERT_TRUE(legacy_only.matches_exact_invariants(make_legacy_ios_hello()));
  ASSERT_FALSE(legacy_only.matches_exact_invariants(make_modern_ios_hello()));

  // Modern baseline with only supported_groups.
  FamilyLaneBaseline modern_groups_only{};
  modern_groups_only.family_id = Slice("apple_ios_tls_modern");
  modern_groups_only.route_lane = Slice("non_ru_egress");
  modern_groups_only.tier = TierLevel::Tier2;
  modern_groups_only.raw_tier = TierLevel::Tier2;
  modern_groups_only.sample_count = 2;
  modern_groups_only.authoritative_sample_count = 2;
  modern_groups_only.num_sources = 1;
  modern_groups_only.num_sessions = 2;
  modern_groups_only.invariants.family_id = modern_groups_only.family_id;
  modern_groups_only.invariants.route_lane = modern_groups_only.route_lane;
  modern_groups_only.invariants.non_grease_supported_groups = {0x11ECu, 0x001Du, 0x0017u, 0x0018u, 0x0019u};
  modern_groups_only.invariants.tls_record_version = 0x0301u;
  modern_groups_only.invariants.client_hello_legacy_version = 0x0303u;

  FamilyLaneMatcher modern_only(modern_groups_only);
  ASSERT_TRUE(modern_only.matches_exact_invariants(make_modern_ios_hello()));
  ASSERT_FALSE(modern_only.matches_exact_invariants(make_legacy_ios_hello()));
}

// ===========================================================================
// TEST 8: Supported versions vector length itself is a discriminator.
//
// This test directly inspects the supported_versions extension body to prove
// that the two generations differ in vector length (4 vs 2), making pooling
// detectable even if every other field were somehow identical. A passive
// censor parsing the supported_versions extension can trivially bucket
// connections by list length.
// ===========================================================================

TEST(IosFamilySplitAdversarial, SupportedVersionsLengthDiscriminatesGenerations) {
  auto legacy_hello = make_legacy_ios_hello();
  auto modern_hello = make_modern_ios_hello();

  // Find supported_versions (0x002B) in each hello.
  const auto *legacy_sv = td::mtproto::test::find_extension(legacy_hello, 0x002Bu);
  const auto *modern_sv = td::mtproto::test::find_extension(modern_hello, 0x002Bu);
  ASSERT_TRUE(legacy_sv != nullptr);
  ASSERT_TRUE(modern_sv != nullptr);

  auto legacy_versions = parse_supported_versions_from_extension(*legacy_sv);
  auto modern_versions = parse_supported_versions_from_extension(*modern_sv);

  // Legacy: 4 versions [TLS 1.3, 1.2, 1.1, 1.0].
  ASSERT_EQ(4u, legacy_versions.size());
  ASSERT_EQ(kLegacySupportedVersions, legacy_versions);

  // Modern: 2 versions [TLS 1.3, 1.2].
  ASSERT_EQ(2u, modern_versions.size());
  ASSERT_EQ(kModernSupportedVersions, modern_versions);

  // The lengths differ -- pooling would require choosing one or the other
  // and would mismatch real devices in whichever generation was not chosen.
  ASSERT_TRUE(legacy_versions.size() != modern_versions.size());

  // Legacy advertises TLS 1.1 and 1.0; modern does not.
  ASSERT_TRUE(std::find(legacy_versions.begin(), legacy_versions.end(), 0x0302u) != legacy_versions.end());
  ASSERT_TRUE(std::find(legacy_versions.begin(), legacy_versions.end(), 0x0301u) != legacy_versions.end());
  ASSERT_TRUE(std::find(modern_versions.begin(), modern_versions.end(), 0x0302u) == modern_versions.end());
  ASSERT_TRUE(std::find(modern_versions.begin(), modern_versions.end(), 0x0301u) == modern_versions.end());
}

}  // namespace
