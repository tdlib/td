// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/stealth/MockRng.h"
#include "test/stealth/RuntimeServerHelloPairingHelpers.h"
#include "test/stealth/ServerHelloFixtureLoader.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/tests.h"

#include <array>

namespace runtime_serverhello_pairing_light_fuzz {

using td::int32;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_runtime_tls_client_hello;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::TransportConfidence;
using td::mtproto::test::client_hello_advertises_cipher_suite;
using td::mtproto::test::load_server_hello_fixture_relative;
using td::mtproto::test::MockRng;
using td::mtproto::test::non_ru_route;
using td::mtproto::test::pairing_server_hello_path_for_profile;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::RuntimeParamsGuard;
using td::mtproto::test::single_runtime_profile_params;

struct Scenario final {
  BrowserProfile profile;
  const char *domain_prefix;
  int32 unix_time_base;
  td::uint64 seed_base;
};

const std::array<Scenario, 12> kScenarios{{
    {BrowserProfile::Chrome133, "runtime-fuzz-linux-chrome133", 1712330000, 0x83100000u},
    {BrowserProfile::Chrome131, "runtime-fuzz-linux-chrome131", 1712340000, 0x83200000u},
    {BrowserProfile::Chrome120, "runtime-fuzz-linux-chrome120", 1712350000, 0x83300000u},
    {BrowserProfile::Chrome147_Windows, "runtime-fuzz-win-chrome", 1712360000, 0x83000000u},
    {BrowserProfile::Firefox149_Windows, "runtime-fuzz-win-firefox", 1712370000, 0x84000000u},
    {BrowserProfile::Firefox148, "runtime-fuzz-linux-firefox148", 1712375000, 0x83400000u},
    {BrowserProfile::Firefox149_MacOS26_3, "runtime-fuzz-macos-firefox149", 1712377500, 0x83500000u},
    {BrowserProfile::Chrome147_IOSChromium, "runtime-fuzz-ios-chromium", 1712380000, 0x85000000u},
    {BrowserProfile::Safari26_3, "runtime-fuzz-safari", 1712390000, 0x86000000u},
    {BrowserProfile::IOS14, "runtime-fuzz-ios-native", 1712400000, 0x87000000u},
    {BrowserProfile::AppleIosTls, "runtime-fuzz-apple-ios", 1712405000, 0x87500000u},
    {BrowserProfile::Android11_OkHttp_Advisory, "runtime-fuzz-android-okhttp", 1712410000, 0x88000000u},
}};

TEST(TlsRuntimeServerHelloPairingLightFuzz, ReviewedServerCipherRemainsAdvertisedAcrossRuntimeSeeds) {
  RuntimeParamsGuard guard;

  for (const auto &scenario : kScenarios) {
    const auto params = single_runtime_profile_params(scenario.profile, TransportConfidence::Strong);
    ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

    const auto relative = pairing_server_hello_path_for_profile(scenario.profile);
    auto sample_result = load_server_hello_fixture_relative(td::CSlice(relative));
    ASSERT_TRUE(sample_result.is_ok());
    const auto sample = sample_result.move_as_ok();

    for (td::uint32 i = 0; i < 128; i++) {
      const auto unix_time = static_cast<int32>(scenario.unix_time_base + static_cast<int32>(i * 37));
      const td::string domain = td::string(scenario.domain_prefix) + '-' + td::to_string(i) + ".example.com";
      ASSERT_TRUE(pick_runtime_profile(td::Slice(domain), unix_time, params.platform_hints) == scenario.profile);

      MockRng rng(scenario.seed_base + i);
      auto client_hello_wire =
          build_runtime_tls_client_hello(domain, "0123456789secret", unix_time, non_ru_route(), rng);
      auto client_hello = parse_tls_client_hello(client_hello_wire);
      ASSERT_TRUE(client_hello.is_ok());
      ASSERT_TRUE(client_hello_advertises_cipher_suite(client_hello.ok_ref().cipher_suites, sample.cipher_suite));
    }
  }
}

}  // namespace runtime_serverhello_pairing_light_fuzz
