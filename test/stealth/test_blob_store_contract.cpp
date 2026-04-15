// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/BlobStore.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::BlobRole;
using td::mtproto::BlobStore;

TEST(BlobStoreContract, SlotValuesMatchReferenceTable) {
  ASSERT_EQ(static_cast<td::int64>(0xd09d1d85de64fd85ULL), BlobStore::expected_slot(BlobRole::Primary));
  ASSERT_EQ(static_cast<td::int64>(0xb25898df208d2603ULL), BlobStore::expected_slot(BlobRole::Secondary));
  ASSERT_EQ(static_cast<td::int64>(0x6f3a701151477715ULL), BlobStore::expected_slot(BlobRole::Auxiliary));
}

TEST(BlobStoreContract, BundleCheckSucceedsForBundledMaterial) {
  ASSERT_TRUE(BlobStore::verify_bundle().is_ok());
}

TEST(BlobStoreContract, LoadReturnsPrimarySlot) {
  auto rsa = BlobStore::load(BlobRole::Primary).move_as_ok();
  ASSERT_EQ(BlobStore::expected_slot(BlobRole::Primary), rsa.get_fingerprint());
}

TEST(BlobStoreContract, LoadReturnsSecondarySlot) {
  auto rsa = BlobStore::load(BlobRole::Secondary).move_as_ok();
  ASSERT_EQ(BlobStore::expected_slot(BlobRole::Secondary), rsa.get_fingerprint());
}

TEST(BlobStoreContract, LoadReturnsAuxiliarySlot) {
  auto rsa = BlobStore::load(BlobRole::Auxiliary).move_as_ok();
  ASSERT_EQ(BlobStore::expected_slot(BlobRole::Auxiliary), rsa.get_fingerprint());
}

}  // namespace