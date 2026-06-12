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

TEST(StealthConfigDarwinProfileSourceContract,
     EmulateTlsPathUsesRuntimeProfileSelectionWithoutCompileTimeDarwinBypass) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/stealth/StealthConfig.cpp");
  auto region = extract_source_region(source, "StealthConfig StealthConfig::from_secret(",
                                      "Status StealthConfig::validate() const {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(secret.emulate_tls()){") != td::string::npos);
  ASSERT_TRUE(normalized.find("config.profile=pick_runtime_profile(secret.get_domain(),unix_time,platform);") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("apply_profile_record_size_limit(config,platform);") != td::string::npos);
  ASSERT_TRUE(normalized.find("config.padding_policy.enabled=false;") != td::string::npos);
  ASSERT_TRUE(normalized.find("config.greeting_camouflage_policy=make_default_greeting_camouflage_policy();") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("config.chaff_policy.enabled=true;") != td::string::npos);
  ASSERT_TRUE(normalized.find("TD_DARWIN") == td::string::npos);
}

}  // namespace
