// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/stealth/MockRng.h"
#include "test/stealth/RuntimeServerHelloPairingHelpers.h"
#include "test/stealth/ServerHelloFixtureLoader.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

#include <array>

namespace runtime_serverhello_pairing_adversarial {

using td::int32;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_runtime_tls_client_hello;
using td::mtproto::stealth::get_runtime_ech_decision;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::TransportConfidence;
using td::mtproto::test::client_hello_advertises_cipher_suite;
using td::mtproto::test::find_extension;
using td::mtproto::test::load_server_hello_fixture_relative;
using td::mtproto::test::MockRng;
using td::mtproto::test::pairing_server_hello_path_for_profile;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::parse_tls_server_hello;
using td::mtproto::test::ru_route;
using td::mtproto::test::RuntimeParamsGuard;
using td::mtproto::test::single_runtime_profile_params;
using td::mtproto::test::synthesize_server_hello_wire;

struct Scenario final {
  BrowserProfile profile;
  const char *domain;
  int32 unix_time;
  td::uint64 seed;
};

const std::array<Scenario, 12> kScenarios{{
    {BrowserProfile::Chrome133, "runtime-adversarial-linux-chrome133.example.com", 1712349001, 0x82000011u},
    {BrowserProfile::Chrome131, "runtime-adversarial-linux-chrome131.example.com", 1712349555, 0x82000012u},
    {BrowserProfile::Chrome120, "runtime-adversarial-linux-chrome120.example.com", 1712349777, 0x82000013u},
    {BrowserProfile::Chrome147_Windows, "runtime-adversarial-win-chrome.example.com", 1712350001, 0x82000001u},
    {BrowserProfile::Firefox149_Windows, "runtime-adversarial-win-firefox.example.com", 1712351112, 0x82000002u},
    {BrowserProfile::Firefox148, "runtime-adversarial-linux-firefox148.example.com", 1712351666, 0x82000014u},
    {BrowserProfile::Firefox149_MacOS26_3, "runtime-adversarial-macos-firefox149.example.com", 1712351888, 0x82000015u},
    {BrowserProfile::Chrome147_IOSChromium, "runtime-adversarial-ios-chromium.example.com", 1712352223, 0x82000003u},
    {BrowserProfile::Safari26_3, "runtime-adversarial-safari.example.com", 1712353334, 0x82000004u},
    {BrowserProfile::IOS14, "runtime-adversarial-ios-native.example.com", 1712354445, 0x82000005u},
    {BrowserProfile::AppleIosTls, "runtime-adversarial-apple-ios.example.com", 1712355001, 0x82000016u},
    {BrowserProfile::Android11_OkHttp_Advisory, "runtime-adversarial-android-okhttp.example.com", 1712355556,
     0x82000006u},
}};

TEST(TlsRuntimeServerHelloPairingAdversarial, RuRouteDisablesEchWithoutDroppingReviewedServerCipher) {
  RuntimeParamsGuard guard;

  for (const auto &scenario : kScenarios) {
    const auto params = single_runtime_profile_params(scenario.profile, TransportConfidence::Strong);
    ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
    const auto domain = td::Slice(scenario.domain);
    ASSERT_TRUE(pick_runtime_profile(domain, scenario.unix_time, params.platform_hints) == scenario.profile);

    auto decision = get_runtime_ech_decision(domain, scenario.unix_time, ru_route());
    ASSERT_TRUE(decision.disabled_by_route);

    const auto relative = pairing_server_hello_path_for_profile(scenario.profile);
    auto sample_result = load_server_hello_fixture_relative(td::CSlice(relative));
    ASSERT_TRUE(sample_result.is_ok());
    const auto sample = sample_result.move_as_ok();

    auto server_hello = parse_tls_server_hello(synthesize_server_hello_wire(sample));
    ASSERT_TRUE(server_hello.is_ok());

    MockRng rng(scenario.seed);
    auto client_hello_wire =
        build_runtime_tls_client_hello(domain.str(), "0123456789secret", scenario.unix_time, ru_route(), rng);
    auto client_hello = parse_tls_client_hello(client_hello_wire);
    ASSERT_TRUE(client_hello.is_ok());
    ASSERT_EQ(0u, client_hello.ok_ref().ech_payload_length);
    ASSERT_TRUE(find_extension(client_hello.ok_ref(), 0xFE0Du) == nullptr);

    ASSERT_TRUE(
        client_hello_advertises_cipher_suite(client_hello.ok_ref().cipher_suites, server_hello.ok_ref().cipher_suite));
  }
}

}  // namespace runtime_serverhello_pairing_adversarial
