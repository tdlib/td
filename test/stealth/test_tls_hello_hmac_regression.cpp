// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <array>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::build_proxy_tls_client_hello_for_profile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::MockRng;

constexpr size_t kClientRandomOffset = 11;
constexpr size_t kClientRandomLength = 32;
constexpr size_t kTimestampTailOffset = 28;
constexpr size_t kTimestampTailLength = 4;

td::uint32 load_le_u32(const td::string &bytes, size_t offset) {
  CHECK(offset + kTimestampTailLength <= bytes.size());
  return static_cast<td::uint32>(static_cast<td::uint8>(bytes[offset])) |
         (static_cast<td::uint32>(static_cast<td::uint8>(bytes[offset + 1])) << 8) |
         (static_cast<td::uint32>(static_cast<td::uint8>(bytes[offset + 2])) << 16) |
         (static_cast<td::uint32>(static_cast<td::uint8>(bytes[offset + 3])) << 24);
}

void store_le_u32(td::string &bytes, size_t offset, td::uint32 value) {
  CHECK(offset + kTimestampTailLength <= bytes.size());
  bytes[offset] = static_cast<char>(value & 0xFFu);
  bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFFu);
  bytes[offset + 2] = static_cast<char>((value >> 16) & 0xFFu);
  bytes[offset + 3] = static_cast<char>((value >> 24) & 0xFFu);
}

td::string expected_client_random(td::string wire, td::Slice secret, td::int32 unix_time) {
  CHECK(wire.size() >= kClientRandomOffset + kClientRandomLength);
  std::fill(wire.begin() + kClientRandomOffset, wire.begin() + kClientRandomOffset + kClientRandomLength, '\0');

  td::string expected_random(kClientRandomLength, '\0');
  td::hmac_sha256(secret, wire, expected_random);

  auto masked_tail = load_le_u32(expected_random, kTimestampTailOffset) ^ static_cast<td::uint32>(unix_time);
  store_le_u32(expected_random, kTimestampTailOffset, masked_tail);
  return expected_random;
}

void assert_reference_safe_random_masking(const td::string &wire, td::Slice secret, td::int32 unix_time) {
  ASSERT_TRUE(wire.size() >= kClientRandomOffset + kClientRandomLength);
  ASSERT_EQ(expected_client_random(wire, secret, unix_time), wire.substr(kClientRandomOffset, kClientRandomLength));
}

TEST(TlsHelloHmacRegression, DefaultBuilderMatchesReferenceSafeMaskingAcrossRouteModes) {
  td::Slice secret("0123456789secret");

  const std::array<td::int32, 6> timestamps = {{0, 1, -1, 0x7FFFFFFF, -2147483647, 1712345678}};
  const std::array<NetworkRouteHints, 3> routes = {{{false, false}, {true, true}, {true, false}}};

  td::uint64 seed = 1;
  for (const auto &route : routes) {
    for (auto unix_time : timestamps) {
      MockRng rng(seed++);
      auto wire = build_default_tls_client_hello("www.google.com", secret, unix_time, route, rng);
      assert_reference_safe_random_masking(wire, secret, unix_time);
    }
  }
}

TEST(TlsHelloHmacRegression, ProfileBuildersMatchReferenceSafeMaskingAcrossProfilesAndAlpnModes) {
  td::Slice secret("0123456789secret");

  struct Scenario final {
    BrowserProfile profile;
    EchMode ech_mode;
    bool proxy_alpn_only;
    td::uint64 seed;
  };

  const std::array<Scenario, 3> scenarios = {{
      {BrowserProfile::Chrome133, EchMode::Rfc9180Outer, false, 41},
      {BrowserProfile::Chrome133, EchMode::Disabled, true, 43},
      {BrowserProfile::Firefox148, EchMode::Rfc9180Outer, false, 47},
  }};
  const std::array<td::int32, 4> timestamps = {{0, -1, 1712345678, 0x7FFFFFFF}};

  for (const auto &scenario : scenarios) {
    for (size_t i = 0; i < timestamps.size(); i++) {
      auto unix_time = timestamps[i];
      MockRng rng(scenario.seed + static_cast<td::uint64>(i));

      td::string wire;
      if (scenario.proxy_alpn_only) {
        wire = build_proxy_tls_client_hello_for_profile("www.google.com", secret, unix_time, scenario.profile,
                                                        scenario.ech_mode, rng);
      } else {
        wire = build_tls_client_hello_for_profile("www.google.com", secret, unix_time, scenario.profile,
                                                  scenario.ech_mode, rng);
      }

      assert_reference_safe_random_masking(wire, secret, unix_time);
    }
  }
}

}  // namespace