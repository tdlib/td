// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::all_profiles;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::profile_fixture_metadata;
using td::mtproto::stealth::ProfileFixtureSourceKind;
using td::mtproto::stealth::ProfileTrustTier;

bool is_network_derived(ProfileFixtureSourceKind source_kind) {
  return source_kind == ProfileFixtureSourceKind::BrowserCapture ||
         source_kind == ProfileFixtureSourceKind::CurlCffiCapture;
}

TEST(TlsProfileRegistryProvenance, VerifiedProfilesRequireNetworkPrimarySourceAndSnapshotCorroboration) {
  for (auto profile :
       {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120, BrowserProfile::Firefox148,
        BrowserProfile::Firefox149_MacOS26_3}) {
    auto metadata = profile_fixture_metadata(profile);
    ASSERT_TRUE(metadata.trust_tier == ProfileTrustTier::Verified);
    ASSERT_TRUE(is_network_derived(metadata.source_kind));
    ASSERT_TRUE(metadata.has_independent_network_provenance);
    ASSERT_TRUE(metadata.has_utls_snapshot_corroboration);
    ASSERT_TRUE(metadata.release_gating);
  }
}

TEST(TlsProfileRegistryProvenance, AdvisoryProfilesRemainOutOfReleaseVerdictWithoutNetworkClosure) {
  for (auto profile : {BrowserProfile::Safari26_3, BrowserProfile::IOS14, BrowserProfile::Android11_OkHttp_Advisory}) {
    auto metadata = profile_fixture_metadata(profile);
    ASSERT_TRUE(metadata.trust_tier == ProfileTrustTier::Advisory);
    ASSERT_FALSE(metadata.release_gating);
    ASSERT_FALSE(metadata.has_independent_network_provenance);
    ASSERT_FALSE(is_network_derived(metadata.source_kind));
  }
}

TEST(TlsProfileRegistryProvenance, ReleaseGatingProfilesNeverUseAdvisoryCodeSamplesAsPrimarySource) {
  for (auto profile : all_profiles()) {
    auto metadata = profile_fixture_metadata(profile);
    if (!metadata.release_gating) {
      continue;
    }
    ASSERT_TRUE(metadata.source_kind != ProfileFixtureSourceKind::AdvisoryCodeSample);
  }
}

}  // namespace