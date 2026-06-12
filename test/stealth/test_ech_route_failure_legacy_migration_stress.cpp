// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "test/stealth/ech_route_failure_store_test_utils.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::runtime_ech_mode_for_route;

TEST(EchRouteFailureLegacyMigrationStress, BulkLegacyMigrationDoesNotCrossContaminateDestinations) {
  auto store = std::make_shared<td::mtproto::test::EchRouteFailureMemoryKeyValue>();
  td::mtproto::test::ScopedRuntimeEchStore scoped_store(store);

  const td::int32 unix_time = 1712345678;
  auto route = td::mtproto::test::non_ru_route_hints();

  constexpr td::uint32 kCount = 512;
  for (td::uint32 i = 0; i < kCount; i++) {
    td::string destination = "stress-" + td::to_string(i) + ".example.com";
    store->set(td::mtproto::test::legacy_store_key(destination, unix_time),
               td::mtproto::test::serialize_store_entry(/*failures=*/3 + (i % 3), /*blocked=*/true,
                                                        /*remaining_ms=*/180000, td::mtproto::test::now_system_ms()));
  }

  for (td::uint32 i = 0; i < kCount; i++) {
    td::string destination = "stress-" + td::to_string(i) + ".example.com";
    ASSERT_TRUE(EchMode::Disabled == runtime_ech_mode_for_route(destination, unix_time, route));
    ASSERT_TRUE(store->get(td::mtproto::test::legacy_store_key(destination, unix_time)).empty());
    ASSERT_FALSE(store->get(td::mtproto::test::canonical_store_key(destination)).empty());
  }

  ASSERT_EQ(kCount, store->prefix_get("stealth_ech_cb#").size());
}

}  // namespace
