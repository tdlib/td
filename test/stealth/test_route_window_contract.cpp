// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/telegram/net/DcOptions.h"
#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/utils/tests.h"
#include "td/utils/tl_helpers.h"

namespace {

td::string serialize_internal_dc_options(std::initializer_list<td::int32> raw_dc_ids) {
  td::DcOptions dc_options;
  td::int32 octet = 10;
  for (auto raw_dc_id : raw_dc_ids) {
    td::IPAddress ip_address;
    auto status = ip_address.init_ipv4_port(PSTRING() << "149.154.167." << octet, 443);
    CHECK(status.is_ok());
    dc_options.dc_options.emplace_back(td::DcId::internal(raw_dc_id), ip_address);
    octet++;
  }
  return td::serialize(dc_options);
}

TEST(RouteWindowContract, ProductionKnownRouteIdsStayBoundedToTelegramSet) {
  for (td::int32 raw_dc_id = 1; raw_dc_id <= 5; raw_dc_id++) {
    ASSERT_TRUE(td::NetQueryDispatcher::is_known_main_dc_id(raw_dc_id, false));
  }
  ASSERT_FALSE(td::NetQueryDispatcher::is_known_main_dc_id(0, false));
  ASSERT_FALSE(td::NetQueryDispatcher::is_known_main_dc_id(6, false));
  ASSERT_FALSE(td::NetQueryDispatcher::is_known_main_dc_id(999, false));
}

TEST(RouteWindowContract, TestKnownRouteIdsStayBoundedToTelegramSet) {
  for (td::int32 raw_dc_id = 1; raw_dc_id <= 3; raw_dc_id++) {
    ASSERT_TRUE(td::NetQueryDispatcher::is_known_main_dc_id(raw_dc_id, true));
  }
  ASSERT_FALSE(td::NetQueryDispatcher::is_known_main_dc_id(4, true));
  ASSERT_FALSE(td::NetQueryDispatcher::is_known_main_dc_id(1000, true));
}

TEST(RouteWindowContract, PersistedRouteSelectionRejectsUnknownIds) {
  ASSERT_FALSE(td::NetQueryDispatcher::is_persistable_main_dc_id(6, false));
  ASSERT_FALSE(td::NetQueryDispatcher::is_persistable_main_dc_id(4, true));
  ASSERT_TRUE(td::NetQueryDispatcher::is_persistable_main_dc_id(5, false));
  ASSERT_TRUE(td::NetQueryDispatcher::is_persistable_main_dc_id(3, true));
}

TEST(RouteWindowContract, CooldownRejectsRapidMainRouteRotation) {
  ASSERT_TRUE(td::NetQueryDispatcher::is_main_dc_migration_rate_limited(100.0, 100.0));
  ASSERT_TRUE(td::NetQueryDispatcher::is_main_dc_migration_rate_limited(100.0, 399.999));
  ASSERT_FALSE(td::NetQueryDispatcher::is_main_dc_migration_rate_limited(100.0, 400.0));
}

TEST(RouteWindowContract, FileRouteAcceptsKnownDefaultAndRegisteredIds) {
  auto serialized_dc_options = serialize_internal_dc_options({6, 8});

  ASSERT_TRUE(td::NetQueryDispatcher::is_registered_file_dc_id(2, false, serialized_dc_options));
  ASSERT_TRUE(td::NetQueryDispatcher::is_registered_file_dc_id(6, false, serialized_dc_options));
  ASSERT_TRUE(td::NetQueryDispatcher::is_registered_file_dc_id(3, true, td::string()));
}

TEST(RouteWindowContract, FileRouteRejectsUnregisteredAndMalformedIds) {
  auto serialized_dc_options = serialize_internal_dc_options({6});

  ASSERT_FALSE(td::NetQueryDispatcher::is_registered_file_dc_id(0, false, serialized_dc_options));
  ASSERT_FALSE(td::NetQueryDispatcher::is_registered_file_dc_id(7, false, serialized_dc_options));
  ASSERT_FALSE(td::NetQueryDispatcher::is_registered_file_dc_id(4, true, serialized_dc_options));
  ASSERT_FALSE(td::NetQueryDispatcher::is_registered_file_dc_id(6, false, "not-a-dc-options-blob"));
}

}  // namespace