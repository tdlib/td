// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

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

TEST(TlsRuntimeBuilderSourceContract, RuntimeBuilderMustUseUnifiedProfileAndEchDecisionPathWithoutDarwinBypass) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/stealth/TlsHelloBuilder.cpp");
  auto region =
      extract_source_region(source,
                            "string build_runtime_tls_client_hello(string domain, Slice secret, int32 unix_time,\n"
                            "                                      const NetworkRouteHints &route_hints, IRng &rng) {",
                            "string build_runtime_tls_client_hello(string domain, Slice secret, int32 unix_time,\n"
                            "                                      const NetworkRouteHints &route_hints) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("autoplatform=default_runtime_platform_hints();") != td::string::npos);
  ASSERT_TRUE(normalized.find("autoselection=select_runtime_profile_for_attempt(domain,unix_time,platform,route_hints);") !=
              td::string::npos);
  ASSERT_TRUE(
      normalized.find("autoresult=try_build_proxy_tls_client_hello_for_profile(std::move(domain),secret,unix_time,"
                      "selection.profile,selection.hello_uses_ech?EchMode::Rfc9180Outer:EchMode::Disabled,rng);") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find("CHECK(result.is_ok());") != td::string::npos);
  ASSERT_TRUE(normalized.find("returnresult.move_as_ok();") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find("pick_runtime_profile(") == td::string::npos);
  ASSERT_TRUE(normalized.find("get_runtime_ech_decision(") == td::string::npos);
  ASSERT_TRUE(normalized.find("TD_DARWIN") == td::string::npos);
  ASSERT_TRUE(normalized.find("build_default_tls_client_hello(") == td::string::npos);
}

}  // namespace
