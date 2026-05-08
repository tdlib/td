// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::extract_source_region;
using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

TEST(LoggingSecretHygieneSourceContract, ConnectionCreatorMustNotHexDumpProxySecrets) {
  auto source = load_repo_text("td/telegram/net/ConnectionCreator.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("format::as_hex(option.get_secret().get_raw_secret())") == td::string::npos);
  ASSERT_TRUE(normalized.find("LOG(INFO)<<\"Useproxy\"<<to_string(option.get_type())") == td::string::npos);
}

TEST(LoggingSecretHygieneSourceContract, StructuredRouteLogsKeepContextWithoutSecretPayloads) {
  auto source = load_repo_text("td/telegram/net/ConnectionCreator.cpp");
  auto region =
      extract_source_region(source, "RawIpConnectionRoute route;",
                            "Result<mtproto::TransportType> ConnectionCreator::resolve_raw_ip_transport_type(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("tag(\"proxy_mode\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("tag(\"tls_emulation\"") != td::string::npos);
  ASSERT_TRUE(normalized.find("get_raw_secret()") == td::string::npos);
  ASSERT_TRUE(normalized.find("get_proxy_secret()") == td::string::npos);
}

TEST(LoggingSecretHygieneSourceContract, RealTrafficFixturePipelineRemainsRepositoryGrounded) {
  auto manifest = load_repo_text("test/analysis/fixtures/imported/import_manifest.json");

  ASSERT_TRUE(manifest.find("docs/Samples/Traffic dumps/") != td::string::npos);
  ASSERT_TRUE(manifest.find("test/analysis/fixtures/imported/clienthello/") != td::string::npos);
  ASSERT_TRUE(manifest.find("test/analysis/fixtures/imported/serverhello/") != td::string::npos);
}

}  // namespace
