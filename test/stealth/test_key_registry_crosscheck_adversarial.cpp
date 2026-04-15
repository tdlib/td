// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/Handshake.h"
#include "td/mtproto/RsaKeyVault.h"
#include "td/telegram/ConfigManager.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/PublicRsaKeySharedMain.h"
#include "td/telegram/StaticCatalog.h"

#include "td/utils/tests.h"

namespace {

TEST(SlotCatalogAdversarial, EnrollmentRejectsUnexpectedSlot) {
  ASSERT_TRUE(
      td::PublicRsaKeySharedMain::check_enrolled_slot(static_cast<td::int64>(0x1020304050607080ULL), false).is_error());
}

TEST(SlotCatalogAdversarial, HandshakeRejectsUnexpectedSlot) {
  ASSERT_TRUE(
      td::mtproto::AuthKeyHandshake::check_selected_slot(static_cast<td::int64>(0x0123456789abcdefULL)).is_error());
}

TEST(SlotCatalogAdversarial, DispatcherRejectsTestSlotOnProductionPath) {
  ASSERT_TRUE(td::NetQueryDispatcher::check_bound_slot(
                  td::StaticCatalog::pinned_slot(td::mtproto::VaultKeyRole::TestMtproto), false)
                  .is_error());
}

TEST(SlotCatalogAdversarial, PayloadCheckRejectsTransportSlot) {
  ASSERT_TRUE(
      td::check_payload_slot(td::StaticCatalog::pinned_slot(td::mtproto::VaultKeyRole::MainMtproto)).is_error());
}

}  // namespace