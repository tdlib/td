// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Adversarial release-gate pairing coverage:
// - Every release-gating runtime profile must have a reviewed ServerHello fixture.
// - The selected reviewed ServerHello cipher suite must be advertised by the
//   generated ClientHello for that profile.

#include "test/stealth/MockRng.h"
#include "test/stealth/ServerHelloFixtureLoader.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::all_profiles;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::profile_fixture_metadata;
using td::mtproto::stealth::profile_spec;
using td::mtproto::test::load_server_hello_fixture_relative;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_cipher_suite_vector;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::parse_tls_server_hello;
using td::mtproto::test::synthesize_server_hello_wire;

struct PairingCase final {
  BrowserProfile profile;
  const char *server_hello_relative_path;
};

constexpr PairingCase kVerifiedProfilePairings[] = {
    {BrowserProfile::Chrome133, "linux_desktop/chrome146_75_linux_desktop.serverhello.json"},
    {BrowserProfile::Chrome131, "linux_desktop/chrome146_75_linux_desktop.serverhello.json"},
    {BrowserProfile::Chrome120, "linux_desktop/chrome146_75_linux_desktop.serverhello.json"},
    {BrowserProfile::Firefox148, "linux_desktop/firefox148_linux_desktop.serverhello.json"},
    {BrowserProfile::Firefox149_MacOS26_3, "macos/firefox149_macos26_3.serverhello.json"},
};

bool pairing_table_contains_profile(BrowserProfile profile) {
  for (const auto &entry : kVerifiedProfilePairings) {
    if (entry.profile == profile) {
      return true;
    }
  }
  return false;
}

EchMode profile_default_ech_mode(BrowserProfile profile) {
  return profile_spec(profile).allows_ech ? EchMode::Rfc9180Outer : EchMode::Disabled;
}

bool client_hello_advertises_cipher_suite(td::Slice cipher_suites_bytes, td::uint16 target_cipher_suite) {
  auto parsed = parse_cipher_suite_vector(cipher_suites_bytes);
  if (parsed.is_error()) {
    return false;
  }
  for (auto suite : parsed.ok()) {
    if (suite == target_cipher_suite) {
      return true;
    }
  }
  return false;
}

TEST(TLS_VerifiedProfileServerHelloPairingAdversarial, PairingTableCoversEveryReleaseGatingProfile) {
  size_t release_gating_profiles = 0;
  for (auto profile : all_profiles()) {
    const auto metadata = profile_fixture_metadata(profile);
    if (!metadata.release_gating) {
      continue;
    }
    release_gating_profiles++;
    ASSERT_TRUE(pairing_table_contains_profile(profile));
  }
  ASSERT_EQ(release_gating_profiles, sizeof(kVerifiedProfilePairings) / sizeof(kVerifiedProfilePairings[0]));
}

TEST(TLS_VerifiedProfileServerHelloPairingAdversarial, ReviewedServerHelloFixturesResolveToTls13ForReleaseProfiles) {
  for (const auto &entry : kVerifiedProfilePairings) {
    auto sample_result = load_server_hello_fixture_relative(td::CSlice(entry.server_hello_relative_path));
    ASSERT_TRUE(sample_result.is_ok());
    const auto sample = sample_result.move_as_ok();

    const auto wire = synthesize_server_hello_wire(sample);
    auto parsed_server_hello = parse_tls_server_hello(wire);
    ASSERT_TRUE(parsed_server_hello.is_ok());
    ASSERT_EQ(static_cast<td::uint16>(0x0304), parsed_server_hello.ok_ref().supported_version_extension_value);
    ASSERT_TRUE(parsed_server_hello.ok_ref().cipher_suite != 0);
  }
}

TEST(TLS_VerifiedProfileServerHelloPairingAdversarial, ReviewedServerCipherSuiteIsAdvertisedByGeneratedClientHello) {
  for (const auto &entry : kVerifiedProfilePairings) {
    MockRng rng(424242u);
    const auto client_hello_wire = build_tls_client_hello_for_profile(
        "www.google.com", "0123456789secret", 1712345678, entry.profile, profile_default_ech_mode(entry.profile), rng);
    auto parsed_client_hello = parse_tls_client_hello(client_hello_wire);
    ASSERT_TRUE(parsed_client_hello.is_ok());

    auto sample_result = load_server_hello_fixture_relative(td::CSlice(entry.server_hello_relative_path));
    ASSERT_TRUE(sample_result.is_ok());
    const auto sample = sample_result.move_as_ok();

    ASSERT_TRUE(client_hello_advertises_cipher_suite(parsed_client_hello.ok_ref().cipher_suites, sample.cipher_suite));
  }
}

}  // namespace