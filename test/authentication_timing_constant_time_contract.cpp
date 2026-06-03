// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    auto b = static_cast<unsigned char>(c);
    if (b == ' ' || b == '\t' || b == '\r' || b == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

}  // namespace

TEST(AuthenticationTimingConstantTimeContract, HmacTimingValidationUsesWorkAmplifiedMeasurements) {
  auto source = td::mtproto::test::read_repo_text_file("test/stealth/test_authentication_constant_time.cpp");
  auto region =
      normalize_for_contract(extract_region(source, "static void validate_hmac_verification_constant_time() {", "};"));

  ASSERT_TRUE(region.find("constexprintiterations_per_position=") != td::string::npos);
  ASSERT_TRUE(region.find("for(inti=0;i<iterations_per_position;i++)") != td::string::npos);
  ASSERT_TRUE(region.find("accumulated^=constant_time_equals(correct_hmac,test_hmac);") != td::string::npos);
  ASSERT_TRUE(region.find("ASSERT_FALSE(accumulated);") != td::string::npos);
}

TEST(AuthenticationTimingConstantTimeContract, HmacTimingValidationUsesMedianRelativeBound) {
  auto source = td::mtproto::test::read_repo_text_file("test/stealth/test_authentication_constant_time.cpp");
  auto region =
      normalize_for_contract(extract_region(source, "static void validate_hmac_verification_constant_time() {", "};"));

  ASSERT_TRUE(region.find("autosorted=verification_times;") != td::string::npos);
  ASSERT_TRUE(region.find("constautomedian=sorted[sorted.size()/2];") != td::string::npos);
  ASSERT_TRUE(region.find("ASSERT_TRUE(median>0);") != td::string::npos);
  ASSERT_TRUE(region.find("ASSERT_TRUE(t<median*6);") != td::string::npos);
}
