// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/BlobStore.h"
#include "td/mtproto/Handshake.h"
#include "td/mtproto/RSA.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/PublicRsaKeySharedMain.h"
#include "td/telegram/ReferenceTable.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::BlobRole;
using td::mtproto::BlobStore;

TEST(BundleToHandshakeFlow, PrimaryBundleFingerprintSatisfiesCatalogCheck) {
  auto rsa = BlobStore::load(BlobRole::Primary).move_as_ok();
  auto fingerprint = rsa.get_fingerprint();
  ASSERT_EQ(td::ReferenceTable::slot_value(BlobRole::Primary), fingerprint);
  ASSERT_TRUE(td::PublicRsaKeySharedMain::check_catalog_entry(fingerprint, false).is_ok());
}

TEST(BundleToHandshakeFlow, SecondaryBundleFingerprintSatisfiesCatalogCheck) {
  auto rsa = BlobStore::load(BlobRole::Secondary).move_as_ok();
  auto fingerprint = rsa.get_fingerprint();
  ASSERT_EQ(td::ReferenceTable::slot_value(BlobRole::Secondary), fingerprint);
  ASSERT_TRUE(td::PublicRsaKeySharedMain::check_catalog_entry(fingerprint, true).is_ok());
}

TEST(BundleToHandshakeFlow, PrimaryBundleFingerprintSatisfiesWindowCheck) {
  auto rsa = BlobStore::load(BlobRole::Primary).move_as_ok();
  ASSERT_TRUE(td::mtproto::AuthKeyHandshake::check_window_entry(rsa.get_fingerprint()).is_ok());
}

TEST(BundleToHandshakeFlow, SecondaryBundleFingerprintSatisfiesWindowCheck) {
  auto rsa = BlobStore::load(BlobRole::Secondary).move_as_ok();
  ASSERT_TRUE(td::mtproto::AuthKeyHandshake::check_window_entry(rsa.get_fingerprint()).is_ok());
}

TEST(BundleToHandshakeFlow, PrimaryBundleFingerprintSatisfiesSharedCheck) {
  auto rsa = BlobStore::load(BlobRole::Primary).move_as_ok();
  ASSERT_TRUE(td::NetQueryDispatcher::check_shared_entry(rsa.get_fingerprint(), false).is_ok());
}

TEST(BundleToHandshakeFlow, SecondaryBundleFingerprintSatisfiesSharedCheck) {
  auto rsa = BlobStore::load(BlobRole::Secondary).move_as_ok();
  ASSERT_TRUE(td::NetQueryDispatcher::check_shared_entry(rsa.get_fingerprint(), true).is_ok());
}

TEST(BundleToHandshakeFlow, AuxiliaryBundleFingerprintSatisfiesConfigCheck) {
  auto rsa = BlobStore::load(BlobRole::Auxiliary).move_as_ok();
  ASSERT_EQ(td::ReferenceTable::slot_value(BlobRole::Auxiliary), rsa.get_fingerprint());
  ASSERT_TRUE(td::check_config_entry(rsa.get_fingerprint()).is_ok());
}

TEST(BundleToHandshakeFlow, AllEnforcementPointsAgreeOnPrimarySlot) {
  auto rsa = BlobStore::load(BlobRole::Primary).move_as_ok();
  auto fingerprint = rsa.get_fingerprint();
  ASSERT_EQ(td::ReferenceTable::slot_value(BlobRole::Primary), fingerprint);
  ASSERT_TRUE(td::PublicRsaKeySharedMain::check_catalog_entry(fingerprint, false).is_ok());
  ASSERT_TRUE(td::mtproto::AuthKeyHandshake::check_window_entry(fingerprint).is_ok());
  ASSERT_TRUE(td::NetQueryDispatcher::check_shared_entry(fingerprint, false).is_ok());
}

TEST(BundleToHandshakeFlow, AllEnforcementPointsAgreeOnSecondarySlot) {
  auto rsa = BlobStore::load(BlobRole::Secondary).move_as_ok();
  auto fingerprint = rsa.get_fingerprint();
  ASSERT_EQ(td::ReferenceTable::slot_value(BlobRole::Secondary), fingerprint);
  ASSERT_TRUE(td::PublicRsaKeySharedMain::check_catalog_entry(fingerprint, true).is_ok());
  ASSERT_TRUE(td::mtproto::AuthKeyHandshake::check_window_entry(fingerprint).is_ok());
  ASSERT_TRUE(td::NetQueryDispatcher::check_shared_entry(fingerprint, true).is_ok());
}

TEST(BundleToHandshakeFlow, BundleVerificationSucceedsBeforeEnforcement) {
  ASSERT_TRUE(BlobStore::verify_bundle().is_ok());
  auto primary = BlobStore::load(BlobRole::Primary).move_as_ok();
  auto secondary = BlobStore::load(BlobRole::Secondary).move_as_ok();
  auto auxiliary = BlobStore::load(BlobRole::Auxiliary).move_as_ok();
  ASSERT_EQ(td::ReferenceTable::slot_value(BlobRole::Primary), primary.get_fingerprint());
  ASSERT_EQ(td::ReferenceTable::slot_value(BlobRole::Secondary), secondary.get_fingerprint());
  ASSERT_EQ(td::ReferenceTable::slot_value(BlobRole::Auxiliary), auxiliary.get_fingerprint());
}

}  // namespace
