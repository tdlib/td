// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/BlobStore.h"
#include "td/mtproto/Handshake.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/PublicRsaKeySharedMain.h"
#include "td/telegram/ReferenceTable.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::BlobRole;

td::int64 tamper(td::int64 value) {
  return static_cast<td::int64>(static_cast<td::uint64>(value) ^ 0xDEADBEEFULL);
}

TEST(SlotEntryRejection, CatalogRejectsTamperedPrimaryFingerprint) {
  auto tampered = tamper(td::ReferenceTable::slot_value(BlobRole::Primary));
  ASSERT_TRUE(td::PublicRsaKeySharedMain::check_catalog_entry(tampered, false).is_error());
}

TEST(SlotEntryRejection, CatalogRejectsTamperedSecondaryFingerprint) {
  auto tampered = tamper(td::ReferenceTable::slot_value(BlobRole::Secondary));
  ASSERT_TRUE(td::PublicRsaKeySharedMain::check_catalog_entry(tampered, true).is_error());
}

TEST(SlotEntryRejection, WindowRejectsTamperedPrimaryFingerprint) {
  auto tampered = tamper(td::ReferenceTable::slot_value(BlobRole::Primary));
  ASSERT_TRUE(td::mtproto::AuthKeyHandshake::check_window_entry(tampered).is_error());
}

TEST(SlotEntryRejection, WindowRejectsTamperedSecondaryFingerprint) {
  auto tampered = tamper(td::ReferenceTable::slot_value(BlobRole::Secondary));
  ASSERT_TRUE(td::mtproto::AuthKeyHandshake::check_window_entry(tampered).is_error());
}

TEST(SlotEntryRejection, SharedRejectsTamperedPrimaryFingerprint) {
  auto tampered = tamper(td::ReferenceTable::slot_value(BlobRole::Primary));
  ASSERT_TRUE(td::NetQueryDispatcher::check_shared_entry(tampered, false).is_error());
}

TEST(SlotEntryRejection, SharedRejectsTamperedSecondaryFingerprint) {
  auto tampered = tamper(td::ReferenceTable::slot_value(BlobRole::Secondary));
  ASSERT_TRUE(td::NetQueryDispatcher::check_shared_entry(tampered, true).is_error());
}

TEST(SlotEntryRejection, ConfigRejectsTamperedAuxiliaryFingerprint) {
  auto tampered = tamper(td::ReferenceTable::slot_value(BlobRole::Auxiliary));
  ASSERT_TRUE(td::check_config_entry(tampered).is_error());
}

TEST(SlotEntryRejection, CatalogRejectsZeroFingerprint) {
  ASSERT_TRUE(td::PublicRsaKeySharedMain::check_catalog_entry(0, false).is_error());
  ASSERT_TRUE(td::PublicRsaKeySharedMain::check_catalog_entry(0, true).is_error());
}

TEST(SlotEntryRejection, WindowRejectsZeroFingerprint) {
  ASSERT_TRUE(td::mtproto::AuthKeyHandshake::check_window_entry(0).is_error());
}

TEST(SlotEntryRejection, SharedRejectsZeroFingerprint) {
  ASSERT_TRUE(td::NetQueryDispatcher::check_shared_entry(0, false).is_error());
  ASSERT_TRUE(td::NetQueryDispatcher::check_shared_entry(0, true).is_error());
}

TEST(SlotEntryRejection, ConfigRejectsZeroFingerprint) {
  ASSERT_TRUE(td::check_config_entry(0).is_error());
}

}  // namespace
