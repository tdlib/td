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

using td::mtproto::VaultKeyRole;

TEST(SlotCatalogContract, EnrollmentAcceptsPinnedSlots) {
  ASSERT_TRUE(
      td::PublicRsaKeySharedMain::check_enrolled_slot(td::StaticCatalog::pinned_slot(VaultKeyRole::MainMtproto), false)
          .is_ok());
  ASSERT_TRUE(
      td::PublicRsaKeySharedMain::check_enrolled_slot(td::StaticCatalog::pinned_slot(VaultKeyRole::TestMtproto), true)
          .is_ok());
}

TEST(SlotCatalogContract, HandshakeAcceptsPinnedSlots) {
  ASSERT_TRUE(
      td::mtproto::AuthKeyHandshake::check_selected_slot(td::StaticCatalog::pinned_slot(VaultKeyRole::MainMtproto))
          .is_ok());
  ASSERT_TRUE(
      td::mtproto::AuthKeyHandshake::check_selected_slot(td::StaticCatalog::pinned_slot(VaultKeyRole::TestMtproto))
          .is_ok());
}

TEST(SlotCatalogContract, DispatcherAcceptsPinnedSlots) {
  ASSERT_TRUE(td::NetQueryDispatcher::check_bound_slot(td::StaticCatalog::pinned_slot(VaultKeyRole::MainMtproto), false)
                  .is_ok());
  ASSERT_TRUE(td::NetQueryDispatcher::check_bound_slot(td::StaticCatalog::pinned_slot(VaultKeyRole::TestMtproto), true)
                  .is_ok());
}

TEST(SlotCatalogContract, PayloadCheckAcceptsPinnedSlot) {
  ASSERT_TRUE(td::check_payload_slot(td::StaticCatalog::pinned_slot(VaultKeyRole::SimpleConfig)).is_ok());
}

}  // namespace