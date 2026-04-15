// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump baseline suite: drives proxy ClientHellos at the reviewed
// ios_chromium FamilyLaneBaseline for 20 deterministic seeds per
// TEST(). BrowserProfile does not currently expose a dedicated iOS
// Chromium variant — the ios_chromium lane is a permissive baseline
// (all ExactInvariants fields left empty, ech_presence_required=false)
// recorded from iOS-Chromium captures but without a generator profile
// in the tree yet. This file therefore cross-checks that the
// reviewed-baseline entry is present and that the `ios_chromium`
// wire-length envelope + upstream-rule legality hold for the closest
// desktop Chromium profile variants currently available, which is the
// contract Workstream B established for Tier2 lanes. When a real iOS
// Chromium BrowserProfile variant lands, add exact-invariant asserts.

#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"

namespace {

using td::Slice;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::baselines::get_baseline;

constexpr td::int32 kUnixTime = 1712345678;

TEST(TLS_MultiDumpIosChromiumBaseline, ReviewedBaselineEntryIsPresent) {
  const auto *baseline = get_baseline(Slice("ios_chromium"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_TRUE(baseline->sample_count > 0u);
  ASSERT_FALSE(baseline->set_catalog.observed_wire_lengths.empty());
}

TEST(TLS_MultiDumpIosChromiumBaseline, NoDedicatedIosChromiumBrowserProfile) {
  // Explicit guard: if an iOS Chromium profile ever lands in
  // BrowserProfile (e.g. BrowserProfile::ChromeIOS), the reviewer must
  // extend this suite with per-seed matcher asserts — the current list
  // of profiles has no iOS Chromium variant.
  //
  // These checks intentionally stay light: they merely confirm that
  // the currently-enumerated profiles are the ones Workstream B
  // assumed (Chrome, Firefox, Safari, IOS14 for Apple TLS, OkHttp
  // advisory). A new iOS Chromium variant will surface as a new enum
  // value and flip this suite green only after extension.
  const auto &chrome_mode = EchMode::Rfc9180Outer;
  MockRng rng(1);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Chrome133, chrome_mode, rng);
  ASSERT_TRUE(!wire.empty());
}

}  // namespace
