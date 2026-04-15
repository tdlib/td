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

TEST(EntryWindowAdversarial, CatalogEntryRejectsUnexpectedValue) {
  ASSERT_TRUE(
      td::PublicRsaKeySharedMain::check_catalog_entry(static_cast<td::int64>(0x1020304050607080ULL), false).is_error());
}

TEST(EntryWindowAdversarial, HandshakeEntryRejectsUnexpectedValue) {
  ASSERT_TRUE(
      td::mtproto::AuthKeyHandshake::check_window_entry(static_cast<td::int64>(0x0123456789abcdefULL)).is_error());
}

TEST(EntryWindowAdversarial, DispatcherEntryRejectsSecondaryValueOnPrimaryPath) {
  ASSERT_TRUE(td::NetQueryDispatcher::check_shared_entry(
                  td::ReferenceTable::slot_value(td::mtproto::BlobRole::Secondary), false)
                  .is_error());
}

TEST(EntryWindowAdversarial, ConfigEntryRejectsPrimaryValue) {
  ASSERT_TRUE(td::check_config_entry(td::ReferenceTable::slot_value(td::mtproto::BlobRole::Primary)).is_error());
}

}  // namespace