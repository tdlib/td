// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Edge case tests: The padding policy has boundary transitions at
// 0xFF and 0x200 bytes. A DPI could detect the padding algorithm
// by observing how total ClientHello sizes cluster around these
// boundaries. These tests verify correct behavior at the edges.

#include "td/mtproto/stealth/Interfaces.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::no_padding_policy;
using td::mtproto::stealth::PaddingPolicy;
using td::mtproto::stealth::resolve_padding_extension_payload_len;

TEST(TlsPaddingBoundaryEdgeCases, BelowLowerBoundMustReturnZero) {
  PaddingPolicy policy;
  // Below 0xFF: no padding
  ASSERT_EQ(0u, policy.compute_padding_content_len(0xFE));
  ASSERT_EQ(0u, policy.compute_padding_content_len(0));
  ASSERT_EQ(0u, policy.compute_padding_content_len(1));
}

TEST(TlsPaddingBoundaryEdgeCases, ExactLowerBoundMustReturnZero) {
  PaddingPolicy policy;
  ASSERT_EQ(0u, policy.compute_padding_content_len(0xFF));
}

TEST(TlsPaddingBoundaryEdgeCases, JustAboveLowerBoundMustPadToAlignmentTarget) {
  PaddingPolicy policy;
  // 0x100: 0x200 - 0x100 = 256; 256 >= 5 → return 252 (256 - 4)
  auto result = policy.compute_padding_content_len(0x100);
  ASSERT_EQ(252u, result);
}

TEST(TlsPaddingBoundaryEdgeCases, JustBelowUpperBoundMustReturnSmallPadding) {
  PaddingPolicy policy;
  // 0x1FF: 0x200 - 0x1FF = 1; 1 < 5 → return 1
  ASSERT_EQ(1u, policy.compute_padding_content_len(0x1FF));
}

TEST(TlsPaddingBoundaryEdgeCases, NearUpperThresholdSmallGapMustReturnMinimumPad) {
  PaddingPolicy policy;
  // 0x1FC: 0x200 - 0x1FC = 4; 4 < 5 → return 1 (minimum)
  ASSERT_EQ(1u, policy.compute_padding_content_len(0x1FC));
}

TEST(TlsPaddingBoundaryEdgeCases, ExactAlignmentMustPadTo512MinusOverhead) {
  PaddingPolicy policy;
  // 0x1FB: 0x200 - 0x1FB = 5; 5 >= 5 → return 1 (5-4)
  ASSERT_EQ(1u, policy.compute_padding_content_len(0x1FB));
}

TEST(TlsPaddingBoundaryEdgeCases, AtOrAboveUpperBoundMustReturnZero) {
  PaddingPolicy policy;
  ASSERT_EQ(0u, policy.compute_padding_content_len(0x200));
  ASSERT_EQ(0u, policy.compute_padding_content_len(0x201));
  ASSERT_EQ(0u, policy.compute_padding_content_len(0x300));
  ASSERT_EQ(0u, policy.compute_padding_content_len(0xFFFF));
}

TEST(TlsPaddingBoundaryEdgeCases, DisabledPolicyMustAlwaysReturnZero) {
  auto policy = no_padding_policy();
  ASSERT_EQ(0u, policy.compute_padding_content_len(0));
  ASSERT_EQ(0u, policy.compute_padding_content_len(0x100));
  ASSERT_EQ(0u, policy.compute_padding_content_len(0x1FF));
  ASSERT_EQ(0u, policy.compute_padding_content_len(0x200));
}

TEST(TlsPaddingBoundaryEdgeCases, ResolverMustPreferPaddingContentOverEntropy) {
  PaddingPolicy policy;
  // When padding_content_len > 0, entropy is ignored.
  auto result = resolve_padding_extension_payload_len(policy, 0x100, 32);
  auto content_only = policy.compute_padding_content_len(0x100);
  ASSERT_EQ(content_only, result);
}

TEST(TlsPaddingBoundaryEdgeCases, ResolverMustUseEntropyWhenNoPaddingContent) {
  PaddingPolicy policy;
  // 0x50 is below the padding range, so padding_content_len = 0.
  // With entropy_len = 16, resolver returns 1 + 16 = 17.
  auto result = resolve_padding_extension_payload_len(policy, 0x50, 16);
  ASSERT_EQ(17u, result);
}

TEST(TlsPaddingBoundaryEdgeCases, ResolverMustReturnZeroWhenBothAreZero) {
  PaddingPolicy policy;
  auto result = resolve_padding_extension_payload_len(policy, 0x50, 0);
  ASSERT_EQ(0u, result);
}

TEST(TlsPaddingBoundaryEdgeCases, MonotonicDecreaseInPaddingAsLengthApproaches512) {
  PaddingPolicy policy;
  // As unpadded_len increases from 0x100 to 0x1FF, padding should decrease.
  size_t prev = 999;
  for (size_t len = 0x100; len < 0x200; len++) {
    auto padding = policy.compute_padding_content_len(len);
    if (padding > 0 && prev < 999) {
      ASSERT_TRUE(padding <= prev);
    }
    prev = padding;
  }
}

}  // namespace
