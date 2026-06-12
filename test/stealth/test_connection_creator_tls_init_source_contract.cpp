// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    auto byte = static_cast<unsigned char>(c);
    if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string extract_source_region(td::Slice source, td::Slice begin_marker, td::Slice end_marker) {
  auto source_text = source.str();
  auto begin = source_text.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source_text.find(end_marker.str(), begin);
  CHECK(end != td::string::npos);
  CHECK(begin < end);
  return source_text.substr(begin, end - begin);
}

TEST(ConnectionCreatorTlsInitSourceContract, TlsBranchWiresRouteHintsAndFailureStoreBeforeActorCreation) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.cpp");
  auto prepare_connection = extract_source_region(source, "ActorOwn<> ConnectionCreator::prepare_connection(",
                                                  "void ConnectionCreator::client_loop(ClientInfo &client)");
  auto normalized = normalize_for_contract(prepare_connection);

  auto hints_pos =
      normalized.find("route_hints_from_country_code(G()->get_option_string(\"stealth_route_country_code\"))");
  auto store_pos = normalized.find("set_runtime_ech_failure_store(G()->td_db()->get_config_pmc_shared())");
  auto actor_pos = normalized.find("create_actor<mtproto::TlsInit>(");

  ASSERT_TRUE(hints_pos != td::string::npos);
  ASSERT_TRUE(store_pos != td::string::npos);
  ASSERT_TRUE(actor_pos != td::string::npos);
  ASSERT_TRUE(hints_pos < actor_pos);
  ASSERT_TRUE(store_pos < actor_pos);

  ASSERT_TRUE(normalized.find("transport_type.secret.get_domain()") != td::string::npos);
  ASSERT_TRUE(normalized.find("transport_type.secret.get_proxy_secret().str()") != td::string::npos);
}

TEST(ConnectionCreatorTlsInitSourceContract, StartUpInitializesStealthPersistenceStore) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/net/ConnectionCreator.cpp");
  auto start_up = extract_source_region(source, "void ConnectionCreator::start_up() {",
                                        "void ConnectionCreator::init_proxies()");
  auto normalized = normalize_for_contract(start_up);

  auto store_pos = normalized.find("set_runtime_ech_failure_store(G()->td_db()->get_config_pmc_shared())");
  auto init_pos = normalized.find("is_inited_=true;");

  ASSERT_TRUE(store_pos != td::string::npos);
  ASSERT_TRUE(init_pos != td::string::npos);
  ASSERT_TRUE(store_pos < init_pos);
}

}  // namespace
