// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Single-selection handoff (audit H1) + iOS-share floor (audit M2).
//
// The connection path computes one profile per attempt and threads it to both the
// transport-shaping StealthConfig and the emitted ClientHello. These tests pin the
// receiving contract: the explicit-profile StealthConfig path embeds the given
// profile verbatim (no second selection), so a profile chosen by
// pick_runtime_profile_adaptive and threaded through both sides cannot diverge.
// They also pin the M2 policy floor that keeps the verified iOS lanes reachable.

#include "MockRng.h"
#include "RuntimeProfileRotationTestSupport.h"

#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/stealth/StealthConfig.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using namespace td::mtproto::stealth;
using namespace td::mtproto::stealth::rotation_test;
using td::mtproto::BrowserProfile;
using td::mtproto::ProxySecret;
using td::mtproto::test::MockRng;

constexpr td::int32 kUnixTime = 1712345678;

ProxySecret make_tls_secret(const td::string &domain) {
  td::string raw;
  raw.push_back(static_cast<char>(0xee));
  raw += "0123456789secret";
  raw += domain;
  return ProxySecret::from_raw(raw);
}

// The explicit-profile StealthConfig path embeds exactly the supplied profile for
// every candidate, performing no second selection.
TEST(RuntimeProfileRotationHandoff, ExplicitProfileStealthConfigEmbedsSuppliedProfile) {
  RotationTestGuard guard;
  auto secret = make_tls_secret("handoff.example.com");
  ASSERT_TRUE(secret.emulate_tls());

  const BrowserProfile profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Firefox148,
                                     BrowserProfile::AppleIosTls, BrowserProfile::IOS14,
                                     BrowserProfile::AndroidChromium_Alps};
  for (auto profile : profiles) {
    MockRng rng(7);
    auto config = StealthConfig::from_secret(secret, rng, kUnixTime, linux_platform(), profile);
    ASSERT_TRUE(config.profile == profile);
    ASSERT_TRUE(config.validate().is_ok());
  }
}

// make_transport_stealth_config(secret, rng, profile) — the create_transport
// receiver — embeds the supplied profile too.
TEST(RuntimeProfileRotationHandoff, ExplicitProfileTransportConfigEmbedsSuppliedProfile) {
  RotationTestGuard guard;
  auto secret = make_tls_secret("handoff-transport.example.com");
  MockRng rng(11);
  auto config = make_transport_stealth_config(secret, rng, BrowserProfile::AppleIosTls);
  ASSERT_TRUE(config.is_ok());
  ASSERT_TRUE(config.ok().profile == BrowserProfile::AppleIosTls);
}

// The profile chosen adaptively, when threaded to the explicit StealthConfig path,
// makes the shaping config carry the exact same profile as the ClientHello would —
// the coherence the handoff guarantees.
TEST(RuntimeProfileRotationHandoff, AdaptiveChoiceThreadsCoherentlyIntoConfig) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  const td::string dest = "coherent.example.com";
  auto decision = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);

  auto secret = make_tls_secret(dest);
  MockRng rng(3);
  auto config = StealthConfig::from_secret(secret, rng, kUnixTime, linux_platform(), decision.profile);
  ASSERT_TRUE(config.profile == decision.profile);
}

// M2: the iOS-share policy floor keeps the verified iOS lanes reachable. A policy
// ios14 in [1,6] truncates both verified carves to 0 and is rejected; 0 and >= 7
// are accepted.
TEST(RuntimeProfileRotationHandoff, IosPolicyShareFloorRejectsTruncatingValues) {
  RotationTestGuard guard;
  auto with_ios_share = [](td::uint8 ios14) {
    auto params = default_runtime_stealth_params();
    params.profile_selection.mobile.ios14 = ios14;
    params.profile_selection.mobile.android11_okhttp_advisory = static_cast<td::uint8>(100 - ios14);
    return params;
  };

  for (td::uint8 ok_value :
       {static_cast<td::uint8>(0), static_cast<td::uint8>(7), static_cast<td::uint8>(70), static_cast<td::uint8>(100)}) {
    ASSERT_TRUE(validate_runtime_stealth_params(with_ios_share(ok_value)).is_ok());
  }
  for (td::uint8 bad_value : {static_cast<td::uint8>(1), static_cast<td::uint8>(3), static_cast<td::uint8>(6)}) {
    ASSERT_TRUE(validate_runtime_stealth_params(with_ios_share(bad_value)).is_error());
  }
}

}  // namespace
