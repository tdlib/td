// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/BlobStore.h"
#include "td/telegram/ReferenceTable.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::BlobRole;

TEST(ReferenceTableContract, SlotValuesMatchBundledValues) {
  ASSERT_EQ(static_cast<td::int64>(0xd09d1d85de64fd85ULL), td::ReferenceTable::slot_value(BlobRole::Primary));
  ASSERT_EQ(static_cast<td::int64>(0xb25898df208d2603ULL), td::ReferenceTable::slot_value(BlobRole::Secondary));
  ASSERT_EQ(static_cast<td::int64>(0x6f3a701151477715ULL), td::ReferenceTable::slot_value(BlobRole::Auxiliary));
}

TEST(ReferenceTableContract, StoreViewMatchesReferenceTable) {
  for (auto role : {BlobRole::Primary, BlobRole::Secondary, BlobRole::Auxiliary}) {
    ASSERT_EQ(td::ReferenceTable::slot_value(role), td::mtproto::BlobStore::expected_slot(role));
  }
}

TEST(ReferenceTableContract, HostCatalogStaysExplicitAndOrdered) {
  ASSERT_EQ(6u, td::ReferenceTable::host_count());
  ASSERT_EQ("tcdnb.azureedge.net", td::ReferenceTable::host_name(0));
  ASSERT_EQ("dns.google", td::ReferenceTable::host_name(1));
  ASSERT_EQ("mozilla.cloudflare-dns.com", td::ReferenceTable::host_name(2));
  ASSERT_EQ("firebaseremoteconfig.googleapis.com", td::ReferenceTable::host_name(3));
  ASSERT_EQ("reserve-5a846.firebaseio.com", td::ReferenceTable::host_name(4));
  ASSERT_EQ("firestore.googleapis.com", td::ReferenceTable::host_name(5));
}

TEST(ReferenceTableContract, HostMembershipUsesDedicatedTable) {
  ASSERT_TRUE(td::ReferenceTable::contains_host("dns.google"));
  ASSERT_TRUE(td::ReferenceTable::contains_host("mozilla.cloudflare-dns.com"));
  ASSERT_TRUE(td::ReferenceTable::contains_host("firebaseremoteconfig.googleapis.com"));
}

}  // namespace