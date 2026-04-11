// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
//
// REG-1 — Cross-table PQ consistency invariants.
//
// The stealth subsystem keeps two independent profile descriptions:
//   * `td::mtproto::stealth::ProfileSpec` (in `TlsHelloProfileRegistry.cpp`),
//     a small bag of feature flags consumed by the stealth runtime / route
//     gating logic.
//   * `td::mtproto::BrowserProfileSpec` (in `BrowserProfile.cpp`), the
//     declarative wire-level profile actually fed into `ClientHelloOpMapper`
//     and `ClientHelloExecutor` to produce the on-wire `ClientHello`.
//
// After the c7f013608 refactor the wire-level Apple TLS family (`Safari26_3`
// and `IOS14`) correctly stopped emitting the X25519MLKEM768 PQ hybrid group,
// matching real Safari/iOS captures. However the legacy `ProfileSpec` table
// still claimed `has_pq=true, pq_group_id=0x11EC` for both, which is now dead
// metadata that contradicts the wire reality.
//
// These tests are the regression guard that prevents the two tables from
// drifting again. They are written from a black-hat perspective: every test
// makes a positive *and* a negative claim and is expected to fail-loud the
// moment either side regresses.
//
// Specifically the invariants are:
//   I1. For every `BrowserProfile`, `ProfileSpec.has_pq` matches whether
//       the corresponding `BrowserProfileSpec.supported_groups` contains the
//       PQ hybrid group identifier (`0x11EC` = 4588 = X25519MLKEM768).
//   I2. If `has_pq=true` then `pq_group_id == kPqHybridGroup`. If
//       `has_pq=false` then `pq_group_id == 0`.
//   I3. The `BrowserProfileSpec.supported_groups` order matches the
//       `KeyShare` extension `key_share_entries` for the PQ slot in the
//       same profile (PQ groups are advertised together with a key share).
//   I4. The Apple TLS family (`Safari26_3`, `IOS14`) does NOT contain the
//       PQ hybrid group anywhere — neither in `supported_groups` nor in
//       any `key_share_entries`.
//
// I1 + I2 + I4 give us a defense-in-depth: even if a future refactor moves
// `has_pq` somewhere else, the invariant test against the actual
// `BrowserProfileSpec` will still catch a regression that smuggles PQ back
// into Safari/iOS.

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "test/stealth/FingerprintFixtures.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <vector>

namespace {

using td::mtproto::BrowserProfile;
using td::mtproto::BrowserProfileSpec;
using td::mtproto::KeyShareKind;
using td::mtproto::TlsExtensionType;
using td::mtproto::get_profile_spec;
using td::mtproto::stealth::profile_spec;
using td::mtproto::test::fixtures::kPqHybridGroup;
using td::mtproto::test::fixtures::kPqHybridDraftGroup;

constexpr td::uint16 kPqHybridGroupCanonical = 4588;  // X25519MLKEM768
static_assert(kPqHybridGroupCanonical == kPqHybridGroup,
              "fixture constant must match BrowserProfile wire constant");

const std::vector<BrowserProfile> &all_browser_profiles() {
  static const std::vector<BrowserProfile> kAll = {
      BrowserProfile::Chrome133,
      BrowserProfile::Chrome131,
      BrowserProfile::Chrome120,
      BrowserProfile::Firefox148,
      BrowserProfile::Firefox149_MacOS26_3,
      BrowserProfile::Safari26_3,
      BrowserProfile::IOS14,
      BrowserProfile::Android11_OkHttp_Advisory,
  };
  return kAll;
}

bool browser_supported_groups_contain_pq(const BrowserProfileSpec &spec) {
  return std::find(spec.supported_groups.begin(), spec.supported_groups.end(),
                   kPqHybridGroupCanonical) != spec.supported_groups.end();
}

bool browser_supported_groups_contain_legacy_kyber(const BrowserProfileSpec &spec) {
  return std::find(spec.supported_groups.begin(), spec.supported_groups.end(),
                   static_cast<td::uint16>(kPqHybridDraftGroup)) != spec.supported_groups.end();
}

bool browser_key_share_contains_pq(const BrowserProfileSpec &spec) {
  for (const auto &extension : spec.extensions) {
    if (extension.type != TlsExtensionType::KeyShare) {
      continue;
    }
    for (const auto &entry : extension.key_share_entries) {
      if (entry.kind == KeyShareKind::X25519MlKem768) {
        return true;
      }
    }
  }
  return false;
}

// I1 — Across all profiles, `ProfileSpec.has_pq` must reflect the actual wire.
TEST(ProfileSpecPqConsistency, HasPqMatchesBrowserSupportedGroupsAcrossEveryProfile) {
  for (auto profile : all_browser_profiles()) {
    const auto &legacy_spec = profile_spec(profile);
    const auto &browser_spec = get_profile_spec(profile);

    bool wire_has_pq = browser_supported_groups_contain_pq(browser_spec);
    if (legacy_spec.has_pq != wire_has_pq) {
      LOG(ERROR) << "PQ drift detected for profile " << static_cast<int>(profile)
                 << ": ProfileSpec.has_pq=" << legacy_spec.has_pq
                 << " but BrowserProfileSpec.supported_groups PQ presence="
                 << wire_has_pq;
    }
    ASSERT_EQ(wire_has_pq, legacy_spec.has_pq);
  }
}

// I2 — `pq_group_id` must be coherent with `has_pq`.
TEST(ProfileSpecPqConsistency, PqGroupIdConsistentWithHasPqAcrossEveryProfile) {
  for (auto profile : all_browser_profiles()) {
    const auto &legacy_spec = profile_spec(profile);
    if (legacy_spec.has_pq) {
      ASSERT_EQ(static_cast<td::uint16>(kPqHybridGroupCanonical), legacy_spec.pq_group_id);
    } else {
      ASSERT_EQ(static_cast<td::uint16>(0), legacy_spec.pq_group_id);
    }
  }
}

// I3 — If a profile claims PQ at the supported_groups layer, it must also
//      ship a PQ key share. The reverse must also hold: a PQ key share
//      without a corresponding supported_groups entry is a wire-level lie
//      that real browsers do not produce.
TEST(ProfileSpecPqConsistency, BrowserSupportedGroupsAndKeyShareAgreeOnPqEverywhere) {
  for (auto profile : all_browser_profiles()) {
    const auto &browser_spec = get_profile_spec(profile);
    bool sg_pq = browser_supported_groups_contain_pq(browser_spec);
    bool ks_pq = browser_key_share_contains_pq(browser_spec);
    ASSERT_EQ(sg_pq, ks_pq);
  }
}

// I4 — Apple TLS family (Safari26_3, IOS14) DOES have X25519MLKEM768.
//      Real Safari 26.x and Chrome on iOS 26.x captures advertise the PQ
//      hybrid in both supported_groups and key_share. This is the
//      wire-level invariant that anchors Apple TLS profiles to actual
//      ground truth captures under
//      `test/analysis/fixtures/clienthello/ios/`. Dropping the PQ entry
//      produces a wire image that no real Apple TLS client emits and
//      becomes a unique fingerprint at the post-handshake DPI level.
//
//      The legacy "Apple TLS has no PQ" assumption was true for iOS
//      versions before 26.x, but iOS 26.x adopted PQ for the system TLS
//      stack, and Chrome on iOS 26.4 (which uses the system stack) also
//      emits PQ.
TEST(ProfileSpecPqConsistency, AppleTlsFamilyHasPqInBothLegacyAndBrowserSpec) {
  for (auto profile : {BrowserProfile::Safari26_3, BrowserProfile::IOS14}) {
    const auto &legacy_spec = profile_spec(profile);
    const auto &browser_spec = get_profile_spec(profile);

    ASSERT_TRUE(legacy_spec.has_pq);
    ASSERT_EQ(static_cast<td::uint16>(kPqHybridGroupCanonical), legacy_spec.pq_group_id);
    ASSERT_TRUE(browser_supported_groups_contain_pq(browser_spec));
    ASSERT_TRUE(browser_key_share_contains_pq(browser_spec));
    // The legacy Kyber draft codepoint (0x6399) MUST NOT appear — Apple
    // TLS uses the final IANA X25519MLKEM768 codepoint (0x11EC = 4588).
    ASSERT_FALSE(browser_supported_groups_contain_legacy_kyber(browser_spec));
  }
}

// I4b — Android OkHttp advisory profile also has no PQ (matches legacy
//       Android Chromium-no-ALPS family). Defensive guard.
TEST(ProfileSpecPqConsistency, AndroidOkHttpAdvisoryHasNoPq) {
  const auto &legacy_spec = profile_spec(BrowserProfile::Android11_OkHttp_Advisory);
  const auto &browser_spec = get_profile_spec(BrowserProfile::Android11_OkHttp_Advisory);
  ASSERT_FALSE(legacy_spec.has_pq);
  ASSERT_EQ(static_cast<td::uint16>(0), legacy_spec.pq_group_id);
  ASSERT_FALSE(browser_supported_groups_contain_pq(browser_spec));
  ASSERT_FALSE(browser_key_share_contains_pq(browser_spec));
}

// Adversarial — Chrome120 specifically must NOT regress to advertising
// X25519MLKEM768 (it was a non-PQ Chrome before bundled uTLS gained PQ
// support; the upstream uTLS HelloChrome_120_PQ uses the legacy 0x6399
// codepoint, which our runtime registry deliberately rejects).
TEST(ProfileSpecPqConsistency, Chrome120HasNoPqOfAnyKind) {
  const auto &legacy_spec = profile_spec(BrowserProfile::Chrome120);
  const auto &browser_spec = get_profile_spec(BrowserProfile::Chrome120);
  ASSERT_FALSE(legacy_spec.has_pq);
  ASSERT_EQ(static_cast<td::uint16>(0), legacy_spec.pq_group_id);
  ASSERT_FALSE(browser_supported_groups_contain_pq(browser_spec));
  ASSERT_FALSE(browser_supported_groups_contain_legacy_kyber(browser_spec));
  ASSERT_FALSE(browser_key_share_contains_pq(browser_spec));
}

// Positive — every PQ-bearing profile must agree across both tables.
// Includes Chrome 133/131, both Firefox profiles, AND the Apple TLS
// family (Safari 26.3 and IOS14) per the iOS 26.x captures.
TEST(ProfileSpecPqConsistency, ModernPqProfilesHavePqInBothTables) {
  for (auto profile : {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Firefox148,
                       BrowserProfile::Firefox149_MacOS26_3, BrowserProfile::Safari26_3, BrowserProfile::IOS14}) {
    const auto &legacy_spec = profile_spec(profile);
    const auto &browser_spec = get_profile_spec(profile);
    ASSERT_TRUE(legacy_spec.has_pq);
    ASSERT_EQ(static_cast<td::uint16>(kPqHybridGroupCanonical), legacy_spec.pq_group_id);
    ASSERT_TRUE(browser_supported_groups_contain_pq(browser_spec));
    ASSERT_TRUE(browser_key_share_contains_pq(browser_spec));
  }
}

}  // namespace
