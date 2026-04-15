// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/BlobStore.h"
#include "td/mtproto/Handshake.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/PublicRsaKeySharedMain.h"
#include "td/telegram/ReferenceTable.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::BlobRole;

TEST(EntryWindowContract, CatalogEntryAcceptsReferenceSlots) {
  ASSERT_TRUE(td::PublicRsaKeySharedMain::check_catalog_entry(td::ReferenceTable::slot_value(BlobRole::Primary), false)
                  .is_ok());
  ASSERT_TRUE(td::PublicRsaKeySharedMain::check_catalog_entry(td::ReferenceTable::slot_value(BlobRole::Secondary), true)
                  .is_ok());
}

TEST(EntryWindowContract, HandshakeEntryAcceptsReferenceSlots) {
  ASSERT_TRUE(
      td::mtproto::AuthKeyHandshake::check_window_entry(td::ReferenceTable::slot_value(BlobRole::Primary)).is_ok());
  ASSERT_TRUE(
      td::mtproto::AuthKeyHandshake::check_window_entry(td::ReferenceTable::slot_value(BlobRole::Secondary)).is_ok());
}

TEST(EntryWindowContract, DispatcherEntryAcceptsReferenceSlots) {
  ASSERT_TRUE(
      td::NetQueryDispatcher::check_shared_entry(td::ReferenceTable::slot_value(BlobRole::Primary), false).is_ok());
  ASSERT_TRUE(
      td::NetQueryDispatcher::check_shared_entry(td::ReferenceTable::slot_value(BlobRole::Secondary), true).is_ok());
}

TEST(EntryWindowContract, ConfigEntryAcceptsReferenceSlot) {
  ASSERT_TRUE(td::check_config_entry(td::ReferenceTable::slot_value(BlobRole::Auxiliary)).is_ok());
}

}  // namespace