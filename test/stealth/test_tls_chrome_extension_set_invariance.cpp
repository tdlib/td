// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: Chrome extension SET invariance.
//
// From the research findings: "Chrome does not randomize the SET of
// extensions it sends; it randomizes only the ORDER." A DPI that strips
// order and compares the sorted set of extension types can still identify
// the crypto library. These tests verify that all Chrome connections carry
// the same extension types regardless of permutation seed.

#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <set>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::is_grease_value;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

td::string sorted_extension_set_key(td::Slice wire) {
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  td::vector<td::uint16> non_grease_types;
  for (const auto &ext : parsed.ok().extensions) {
    if (!is_grease_value(ext.type)) {
      non_grease_types.push_back(ext.type);
    }
  }
  std::sort(non_grease_types.begin(), non_grease_types.end());
  td::string key;
  for (auto t : non_grease_types) {
    if (!key.empty()) {
      key += ',';
    }
    char buf[5];
    std::snprintf(buf, sizeof(buf), "%04x", t);
    key += buf;
  }
  return key;
}

td::string ordered_extension_list_key(td::Slice wire) {
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  td::string key;
  for (const auto &ext : parsed.ok().extensions) {
    if (!is_grease_value(ext.type)) {
      if (!key.empty()) {
        key += ',';
      }
      char buf[5];
      std::snprintf(buf, sizeof(buf), "%04x", ext.type);
      key += buf;
    }
  }
  return key;
}

TEST(TlsChromeExtensionSetInvariance, Chrome133SortedSetMustBeIdenticalAcross500Seeds) {
  std::set<td::string> sets;
  for (td::uint64 seed = 0; seed < 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    sets.insert(sorted_extension_set_key(wire));
  }
  // The SORTED set of extensions must be identical across all connections.
  ASSERT_EQ(1u, sets.size());
}

TEST(TlsChromeExtensionSetInvariance, Chrome133OrderMustVaryAcrossSeeds) {
  std::set<td::string> orders;
  for (td::uint64 seed = 0; seed < 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    orders.insert(ordered_extension_list_key(wire));
  }
  // While the SET is identical, the ORDER must vary (Chrome shuffles)
  ASSERT_TRUE(orders.size() > 100u);
}

TEST(TlsChromeExtensionSetInvariance, Chrome131SortedSetMustBeIdenticalAcross500Seeds) {
  std::set<td::string> sets;
  for (td::uint64 seed = 0; seed < 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome131, EchMode::Rfc9180Outer, rng);
    sets.insert(sorted_extension_set_key(wire));
  }
  ASSERT_EQ(1u, sets.size());
}

TEST(TlsChromeExtensionSetInvariance, Chrome131And133MustShareIdenticalExtensionSetWhenEchEnabled) {
  // Chrome131 and Chrome133 share the same template except ALPS codepoint.
  // The extension set must differ only in the ALPS type (0x4469 vs 0x44CD).
  MockRng rng1(42);
  auto wire131 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Chrome131, EchMode::Rfc9180Outer, rng1);
  MockRng rng2(42);
  auto wire133 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng2);

  auto parsed131 = parse_tls_client_hello(wire131);
  auto parsed133 = parse_tls_client_hello(wire133);
  ASSERT_TRUE(parsed131.is_ok());
  ASSERT_TRUE(parsed133.is_ok());

  std::set<td::uint16> ext_types_131;
  std::set<td::uint16> ext_types_133;
  for (const auto &ext : parsed131.ok().extensions) {
    if (!is_grease_value(ext.type)) {
      ext_types_131.insert(ext.type);
    }
  }
  for (const auto &ext : parsed133.ok().extensions) {
    if (!is_grease_value(ext.type)) {
      ext_types_133.insert(ext.type);
    }
  }

  // Same count of extensions
  ASSERT_EQ(ext_types_131.size(), ext_types_133.size());

  // They differ only in ALPS type
  bool found_alps_131 = ext_types_131.count(0x4469) > 0;
  bool found_alps_133 = ext_types_133.count(0x44CD) > 0;
  ASSERT_TRUE(found_alps_131);
  ASSERT_TRUE(found_alps_133);
}

TEST(TlsChromeExtensionSetInvariance, Chrome120SortedSetWithoutEchDiffersFromChrome133WithoutEch) {
  MockRng rng1(42);
  auto wire120 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Chrome120, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire133 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Chrome133, EchMode::Disabled, rng2);

  auto set120 = sorted_extension_set_key(wire120);
  auto set133 = sorted_extension_set_key(wire133);

  // Chrome120 does NOT have PQ group in supported_groups/key_share.
  // Chrome133 has PQ + different ALPS. The extension sets must differ.
  ASSERT_NE(set120, set133);
}

TEST(TlsChromeExtensionSetInvariance, SafariExtensionOrderMustBeIdenticalAcrossAllSeeds) {
  std::set<td::string> orders;
  for (td::uint64 seed = 0; seed < 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Safari26_3, EchMode::Disabled, rng);
    orders.insert(ordered_extension_list_key(wire));
  }
  // Safari does NOT shuffle: order must be identical across all connections.
  ASSERT_EQ(1u, orders.size());
}

TEST(TlsChromeExtensionSetInvariance, IOSFixedProfileExtensionOrderMustBeIdenticalAcrossAllSeeds) {
  std::set<td::string> orders;
  for (td::uint64 seed = 0; seed < 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::IOS14, EchMode::Disabled, rng);
    orders.insert(ordered_extension_list_key(wire));
  }
  ASSERT_EQ(1u, orders.size());
}

TEST(TlsChromeExtensionSetInvariance, AndroidFixedProfileExtensionOrderMustBeIdenticalAcrossAllSeeds) {
  std::set<td::string> orders;
  for (td::uint64 seed = 0; seed < 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Android11_OkHttp_Advisory, EchMode::Disabled, rng);
    orders.insert(ordered_extension_list_key(wire));
  }
  ASSERT_EQ(1u, orders.size());
}

TEST(TlsChromeExtensionSetInvariance, Firefox148ExtensionOrderMustBeIdenticalAcrossAllSeeds) {
  std::set<td::string> orders;
  for (td::uint64 seed = 0; seed < 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Firefox148, EchMode::Disabled, rng);
    orders.insert(ordered_extension_list_key(wire));
  }
  // Firefox uses FixedFromFixture order policy
  ASSERT_EQ(1u, orders.size());
}

TEST(TlsChromeExtensionSetInvariance, EchDisabledLaneMustRemoveEchFromExtensionSet) {
  MockRng rng1(42);
  auto wire_ech = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                     BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng1);
  MockRng rng2(42);
  auto wire_no_ech = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                        BrowserProfile::Chrome133, EchMode::Disabled, rng2);

  auto set_ech = sorted_extension_set_key(wire_ech);
  auto set_no_ech = sorted_extension_set_key(wire_no_ech);

  // ECH disabled must remove 0xFE0D from extension set
  ASSERT_NE(set_ech, set_no_ech);
  ASSERT_TRUE(set_ech.find("fe0d") != td::string::npos);
  ASSERT_TRUE(set_no_ech.find("fe0d") == td::string::npos);
}

}  // namespace
