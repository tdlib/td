// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/ReviewedFamilyLaneBaselines.h"

#include "td/utils/tests.h"

namespace {

using td::Slice;
using td::mtproto::test::baselines::EvidenceFieldStatus;
using td::mtproto::test::baselines::get_baseline;

void assert_release_critical_field_available(EvidenceFieldStatus status) {
  ASSERT_TRUE(status == EvidenceFieldStatus::Exact || status == EvidenceFieldStatus::Catalog ||
              status == EvidenceFieldStatus::Policy);
}

TEST(TlsReleaseSimilarityUnavailableFailClosed, ChromiumWindowsNonRuDoesNotPretendEmptyExactFieldsPass) {
  const auto *baseline = get_baseline(Slice("chromium_windows"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);

  assert_release_critical_field_available(baseline->non_grease_cipher_suites_status);
  assert_release_critical_field_available(baseline->non_grease_extension_set_status);
  assert_release_critical_field_available(baseline->non_grease_supported_groups_status);
}

TEST(TlsReleaseSimilarityUnavailableFailClosed, UnknownRouteLanesRemainUnavailable) {
  const auto *baseline = get_baseline(Slice("chromium_windows"), Slice("unknown"));
  ASSERT_TRUE(baseline != nullptr);

  ASSERT_TRUE(baseline->non_grease_cipher_suites_status == EvidenceFieldStatus::Unavailable);
  ASSERT_TRUE(baseline->non_grease_extension_set_status == EvidenceFieldStatus::Unavailable);
  ASSERT_TRUE(baseline->wire_lengths_status == EvidenceFieldStatus::Unavailable);
}

}  // namespace
