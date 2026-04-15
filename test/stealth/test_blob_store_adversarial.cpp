// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/BlobStore.h"

#include "td/utils/tests.h"

namespace {

TEST(BlobStoreAdversarial, UnknownRoleFailsClosed) {
  auto status = td::mtproto::BlobStore::load(static_cast<td::mtproto::BlobRole>(0x7f));
  ASSERT_TRUE(status.is_error());
}

TEST(BlobStoreAdversarial, UnknownRoleDoesNotPoisonPinnedRoles) {
  ASSERT_TRUE(td::mtproto::BlobStore::load(static_cast<td::mtproto::BlobRole>(0x7f)).is_error());

  auto rsa = td::mtproto::BlobStore::load(td::mtproto::BlobRole::Primary).move_as_ok();
  ASSERT_EQ(td::mtproto::BlobStore::expected_slot(td::mtproto::BlobRole::Primary), rsa.get_fingerprint());
}

TEST(BlobStoreAdversarial, BundleCheckRemainsStableAcrossRepeatedCalls) {
  for (td::uint32 iteration = 0; iteration < 32; iteration++) {
    ASSERT_TRUE(td::mtproto::BlobStore::verify_bundle().is_ok());
  }
}

TEST(BlobStoreAdversarial, RepeatedLoadsKeepSlotsStable) {
  for (auto role :
       {td::mtproto::BlobRole::Primary, td::mtproto::BlobRole::Secondary, td::mtproto::BlobRole::Auxiliary}) {
    auto expected = td::mtproto::BlobStore::expected_slot(role);
    for (td::uint32 iteration = 0; iteration < 16; iteration++) {
      auto rsa = td::mtproto::BlobStore::load(role).move_as_ok();
      ASSERT_EQ(expected, rsa.get_fingerprint());
    }
  }
}

}  // namespace