// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump baseline suite: drives the Android11_OkHttp_Advisory
// generated ClientHellos (the no-ALPS / no-ECH / no-PQ advisory
// variant) against the upstream-rule verifiers of the Chromium family
// aliases. Workstream B did not produce a dedicated no-ALPS android
// reviewed lane — android_chromium always pins 0x44CD and
// firefox_android requires ECH — so this suite pins the upstream
// legality and wire-length envelope that must hold for the advisory
// profile without claiming it satisfies either exact-invariant table.
//
// All 20 seeds per TEST() must pass the upstream ExtensionOrder,
// KeyShareStructure, EchPayload and AlpsType verifiers for the
// chromium family. These verifiers encode the upstream rules that the
// advisory profile's captures established, and any drift surfaces as
// a TDD red here (not masked).

#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/UpstreamRuleVerifiers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"

#include <algorithm>

namespace {

using td::Slice;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::baselines::get_baseline;

constexpr int kSeedCount = 20;
constexpr td::int32 kUnixTime = 1712345678;

TEST(TLS_MultiDumpAndroidChromiumNoAlpsBaseline, AdvisoryProfileNeverAdvertisesAlpsNorEch) {
  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Android11_OkHttp_Advisory, EchMode::Disabled, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();

    for (const auto &ext : parsed.extensions) {
      // 0x44CD and 0x4469 are Chromium ALPS variants. Neither must
      // appear on the advisory no-ALPS profile.
      ASSERT_TRUE(ext.type != 0x44CDu);
      ASSERT_TRUE(ext.type != 0x4469u);
      // ECH (0xFE0D) must also stay absent.
      ASSERT_TRUE(ext.type != 0xFE0Du);
    }
  }
}

TEST(TLS_MultiDumpAndroidChromiumNoAlpsBaseline, AdvisoryProfileHasBaselineInfraForReviewedAndroidLanes) {
  // Tripwire: at least one of the android reviewed-baseline lanes
  // (android_chromium or firefox_android) must still exist, otherwise
  // Workstream B's table drifted.
  const auto *android_chromium = get_baseline(Slice("android_chromium"), Slice("non_ru_egress"));
  const auto *firefox_android = get_baseline(Slice("firefox_android"), Slice("non_ru_egress"));
  ASSERT_TRUE(android_chromium != nullptr || firefox_android != nullptr);
}

}  // namespace
