// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: Chrome's extension shuffle must produce high entropy
// in extension ordering. A DPI that accumulates extension order statistics
// could detect synthetic ClientHello if:
// 1. The permutation entropy is too low (few distinct orderings).
// 2. Some extensions are always in a fixed position (not just anchors).
// 3. The shuffle has a detectable bias toward certain orderings.

#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <cmath>
#include <unordered_set>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::is_grease_value;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

// Build a non-GREASE extension type sequence fingerprint as a string key.
td::string extension_order_fingerprint(td::Slice wire) {
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto hello = parsed.move_as_ok();

  td::string result;
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type)) {
      if (!result.empty()) {
        result += ",";
      }
      result += td::to_string(ext.type);
    }
  }
  return result;
}

// Get position of a specific non-GREASE extension among non-GREASE extensions.
int non_grease_position_of(td::Slice wire, td::uint16 target_type) {
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto hello = parsed.move_as_ok();

  int position = 0;
  for (const auto &ext : hello.extensions) {
    if (is_grease_value(ext.type)) {
      continue;
    }
    if (ext.type == target_type) {
      return position;
    }
    position++;
  }
  return -1;
}

TEST(TlsExtensionOrderEntropyAdversarial, ChromeShuffleMustProduceManyDistinctOrderings) {
  // With ~14 shuffleable extensions, there are 14! > 87 billion orderings.
  // Even with 500 samples, we must see near-100% unique orderings.
  std::unordered_set<td::string> orderings;
  for (td::uint64 seed = 1; seed <= 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    orderings.insert(extension_order_fingerprint(wire));
  }
  // Must see at least 490 unique orderings out of 500 (>98%).
  ASSERT_TRUE(orderings.size() >= 490u);
}

TEST(TlsExtensionOrderEntropyAdversarial, RenegotiationInfoMustNotAlwaysBeLastInShuffledBlock) {
  // Historical bug: RenegotiationInfo (0xFF01) was a tail anchor.
  // Chrome shuffles it freely. Verify it appears at diverse positions.
  std::unordered_set<int> positions;
  for (td::uint64 seed = 1; seed <= 300; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    int pos = non_grease_position_of(wire, 0xFF01);
    if (pos >= 0) {
      positions.insert(pos);
    }
  }
  // RenegotiationInfo should appear at many different positions (at least 5).
  ASSERT_TRUE(positions.size() >= 5u);
}

TEST(TlsExtensionOrderEntropyAdversarial, SniMustAppearAtDiversePositions) {
  // SNI (0x0000) is a shuffleable extension in Chrome. Check diversity.
  std::unordered_set<int> positions;
  for (td::uint64 seed = 1; seed <= 300; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    int pos = non_grease_position_of(wire, 0x0000);  // SNI
    if (pos >= 0) {
      positions.insert(pos);
    }
  }
  ASSERT_TRUE(positions.size() >= 5u);
}

TEST(TlsExtensionOrderEntropyAdversarial, Firefox148MustUseFixedExtensionOrder) {
  // Firefox does NOT shuffle extensions. All connections with the same seed
  // should produce the same ordering, and different seeds should also produce
  // the same core ordering (since it's fixed).
  td::string first_order;
  for (td::uint64 seed = 1; seed <= 50; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Firefox148, EchMode::Disabled, rng);
    auto order = extension_order_fingerprint(wire);
    if (first_order.empty()) {
      first_order = order;
    } else {
      ASSERT_EQ(first_order, order);
    }
  }
}

TEST(TlsExtensionOrderEntropyAdversarial, Chrome131And133MustBothShuffle) {
  // Both Chrome 131 and 133 use ChromeShuffleAnchored. Verify.
  auto check_profile = [](BrowserProfile profile) {
    std::unordered_set<td::string> orderings;
    for (td::uint64 seed = 1; seed <= 100; seed++) {
      MockRng rng(seed);
      auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                     EchMode::Disabled, rng);
      orderings.insert(extension_order_fingerprint(wire));
    }
    return orderings.size();
  };

  ASSERT_TRUE(check_profile(BrowserProfile::Chrome131) >= 95u);
  ASSERT_TRUE(check_profile(BrowserProfile::Chrome133) >= 95u);
}

TEST(TlsExtensionOrderEntropyAdversarial, AlpsPositionMustVaryAcrossConnections) {
  // ALPS (0x44CD for Chrome133) is inside the shuffled block.
  std::unordered_set<int> positions;
  for (td::uint64 seed = 1; seed <= 300; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    int pos = non_grease_position_of(wire, 0x44CD);  // ALPS
    if (pos >= 0) {
      positions.insert(pos);
    }
  }
  ASSERT_TRUE(positions.size() >= 5u);
}

}  // namespace
