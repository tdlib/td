// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Regression for PR #21 review finding 5 (F5): Firefox148 (Linux) and
// Firefox149_MacOS26_3 (macOS) used to alias the single `firefox148` weight
// slot, so an operator could not tune or zero one Firefox lane without also
// disabling the other. These tests prove the slots are now independent: zeroing
// one lane leaves the other selectable. Before the fix, zeroing firefox148 with
// both Firefox profiles allowed drove the sticky selector's total weight to 0
// (CHECK failure) instead of selecting the still-enabled macOS lane.

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/Span.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::pick_profile_sticky;
using td::mtproto::stealth::ProfileWeights;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::SelectionKey;
using td::mtproto::test::MockRng;

constexpr td::uint32 kBuckets = 256;

TEST(FirefoxWeightIndependence, ZeroingLinuxFirefoxLeavesMacosFirefoxSelectable) {
  ProfileWeights weights;
  weights.firefox148 = 0;              // Linux Firefox lane disabled
  weights.firefox149_macos26_3 = 100;  // macOS Firefox lane still enabled

  const BrowserProfile allowed_arr[] = {BrowserProfile::Firefox148, BrowserProfile::Firefox149_MacOS26_3};
  auto allowed = td::Span<BrowserProfile>(allowed_arr);
  RuntimePlatformHints platform{};
  SelectionKey key;
  key.destination = "firefox.example.com";

  for (td::uint32 bucket = 0; bucket < kBuckets; bucket++) {
    key.time_bucket = bucket;
    MockRng rng(static_cast<td::uint64>(bucket) + 1);
    auto picked = pick_profile_sticky(weights, key, platform, allowed, rng);
    ASSERT_TRUE(picked == BrowserProfile::Firefox149_MacOS26_3);
  }
}

TEST(FirefoxWeightIndependence, ZeroingMacosFirefoxLeavesLinuxFirefoxSelectable) {
  ProfileWeights weights;
  weights.firefox148 = 100;          // Linux Firefox lane enabled
  weights.firefox149_macos26_3 = 0;  // macOS Firefox lane disabled

  const BrowserProfile allowed_arr[] = {BrowserProfile::Firefox148, BrowserProfile::Firefox149_MacOS26_3};
  auto allowed = td::Span<BrowserProfile>(allowed_arr);
  RuntimePlatformHints platform{};
  SelectionKey key;
  key.destination = "firefox.example.com";

  for (td::uint32 bucket = 0; bucket < kBuckets; bucket++) {
    key.time_bucket = bucket;
    MockRng rng(static_cast<td::uint64>(bucket) + 1);
    auto picked = pick_profile_sticky(weights, key, platform, allowed, rng);
    ASSERT_TRUE(picked == BrowserProfile::Firefox148);
  }
}

}  // namespace
