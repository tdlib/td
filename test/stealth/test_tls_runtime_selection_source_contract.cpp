// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace tls_runtime_selection_source_contract_test {

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

TEST(TlsRuntimeSelectionSourceContract, AllowedProfilesForPlatformRoutesDesktopMobileAndWindowsToDedicatedSets) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/stealth/TlsHelloProfileRegistry.cpp");
  auto region = extract_source_region(source, "Span<BrowserProfile> allowed_profiles_for_platform(",
                                      "const ProfileSpec &profile_spec(BrowserProfile profile)");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(platform.device_class==DeviceClass::Mobile)") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(platform.mobile_os==MobileOs::IOS)") != td::string::npos);
  ASSERT_TRUE(
      normalized.find("returnSpan<BrowserProfile>(tls_hello_profile_registry_internal::IOS_MOBILE_PROFILES);") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find("if(platform.mobile_os==MobileOs::Android)") != td::string::npos);
  ASSERT_TRUE(
      normalized.find("returnSpan<BrowserProfile>(tls_hello_profile_registry_internal::ANDROID_MOBILE_PROFILES);") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find("returnSpan<BrowserProfile>(tls_hello_profile_registry_internal::MOBILE_PROFILES);") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("if(platform.desktop_os==DesktopOs::Darwin)") != td::string::npos);
  ASSERT_TRUE(
      normalized.find("returnSpan<BrowserProfile>(tls_hello_profile_registry_internal::DARWIN_DESKTOP_PROFILES);") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find("if(platform.desktop_os==DesktopOs::Windows)") != td::string::npos);
  ASSERT_TRUE(
      normalized.find("returnSpan<BrowserProfile>(tls_hello_profile_registry_internal::WINDOWS_DESKTOP_PROFILES);") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find(
                  "returnSpan<BrowserProfile>(tls_hello_profile_registry_internal::NON_DARWIN_DESKTOP_PROFILES);") !=
              td::string::npos);
}

TEST(TlsRuntimeSelectionSourceContract, RuntimeProfileSelectionUsesPlatformAllowListAndStableHashRoll) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/stealth/TlsHelloProfileRegistry.cpp");
  auto region = extract_source_region(source, "RuntimeProfileResolution resolve_runtime_profile(",
                                      "}  // namespace tls_hello_profile_registry_internal");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("autoallowed_profiles=allowed_profiles_for_platform(platform);") != td::string::npos);
  ASSERT_TRUE(normalized.find("resolution.key=make_profile_selection_key(destination,unix_time);") != td::string::npos);
  ASSERT_TRUE(normalized.find("constauto&weights=runtime_params.profile_weights;") != td::string::npos);
  ASSERT_TRUE(normalized.find("std::vector<BrowserProfile>confidence_allowed_profiles;") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(!transport_confidence_allows_profile(runtime_params,profile)){continue;}") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("if(profile_weight(weights,profile)==0){continue;}") != td::string::npos);
  ASSERT_TRUE(normalized.find("autobaseline=weighted_pick(confidence_allowed_profiles,weights,resolution.key,platform);") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("for(autoprofile:confidence_allowed_profiles)") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(confidence_allowed_profiles.empty())") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(!runtime_params.release_mode_profile_gating){resolution.baseline=baseline;resolution.selectable=std::move(confidence_allowed_profiles);returnresolution;}") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("runtime_profile_selection_counters().advisory_blocked_total.fetch_add(1,std::memory_order_relaxed);") !=
              td::string::npos);
}

TEST(TlsRuntimeSelectionSourceContract, PickRuntimeProfileDelegatesToSharedResolutionHelper) {
  auto source = td::mtproto::test::read_repo_text_file("td/mtproto/stealth/TlsHelloProfileRegistry.cpp");
  auto region = extract_source_region(source, "BrowserProfile pick_runtime_profile(",
                                      "RuntimeProfileSelectionDecision pick_runtime_profile_adaptive(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("autoruntime_params=get_runtime_stealth_params_snapshot();") != td::string::npos);
  ASSERT_TRUE(normalized.find("returntls_hello_profile_registry_internal::resolve_runtime_profile(runtime_params,destination,unix_time,platform).baseline;") !=
              td::string::npos);
}

}  // namespace tls_runtime_selection_source_contract_test
