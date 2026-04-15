// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/RsaKeyVault.h"
#include "td/telegram/StaticCatalog.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::VaultKeyRole;

TEST(StaticCatalogContract, PinnedSlotsMatchBundledValues) {
  ASSERT_EQ(static_cast<td::int64>(0xd09d1d85de64fd85ULL), td::StaticCatalog::pinned_slot(VaultKeyRole::MainMtproto));
  ASSERT_EQ(static_cast<td::int64>(0xb25898df208d2603ULL), td::StaticCatalog::pinned_slot(VaultKeyRole::TestMtproto));
  ASSERT_EQ(static_cast<td::int64>(0x6f3a701151477715ULL), td::StaticCatalog::pinned_slot(VaultKeyRole::SimpleConfig));
}

TEST(StaticCatalogContract, VaultFingerprintViewMatchesCatalog) {
  for (auto role : {VaultKeyRole::MainMtproto, VaultKeyRole::TestMtproto, VaultKeyRole::SimpleConfig}) {
    ASSERT_EQ(td::StaticCatalog::pinned_slot(role), td::mtproto::RsaKeyVault::expected_fingerprint(role));
  }
}

TEST(StaticCatalogContract, EndpointCatalogStaysExplicitAndOrdered) {
  ASSERT_EQ(6u, td::StaticCatalog::endpoint_count());
  ASSERT_EQ("tcdnb.azureedge.net", td::StaticCatalog::endpoint_host(0));
  ASSERT_EQ("dns.google", td::StaticCatalog::endpoint_host(1));
  ASSERT_EQ("mozilla.cloudflare-dns.com", td::StaticCatalog::endpoint_host(2));
  ASSERT_EQ("firebaseremoteconfig.googleapis.com", td::StaticCatalog::endpoint_host(3));
  ASSERT_EQ("reserve-5a846.firebaseio.com", td::StaticCatalog::endpoint_host(4));
  ASSERT_EQ("firestore.googleapis.com", td::StaticCatalog::endpoint_host(5));
}

TEST(StaticCatalogContract, EndpointMembershipUsesDedicatedCatalog) {
  ASSERT_TRUE(td::StaticCatalog::has_endpoint_host("dns.google"));
  ASSERT_TRUE(td::StaticCatalog::has_endpoint_host("mozilla.cloudflare-dns.com"));
  ASSERT_TRUE(td::StaticCatalog::has_endpoint_host("firebaseremoteconfig.googleapis.com"));
}

}  // namespace