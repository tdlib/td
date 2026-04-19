// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/telegram/net/ConnectionCreator.h"

#include "td/utils/tests.h"

#include <array>
#include <cstdint>
#include <random>
#include <ranges>

namespace {

td::IPAddress random_ipv4(std::mt19937_64 &rng, td::int32 port) {
  std::array<td::uint32, 4> octets{};
  for (auto &octet : octets) {
    octet = static_cast<td::uint32>(rng() & 0xFFu);
  }

  td::IPAddress result;
  auto status = result.init_ipv4_port(
      PSLICE() << octets[0] << '.' << octets[1] << '.' << octets[2] << '.' << octets[3],
      port);
  status.ensure();
  return result;
}

td::IPAddress random_ipv6(std::mt19937_64 &rng, td::int32 port) {
  constexpr std::array<td::CSlice, 6> kIpv6Pool = {
      "::1",
      "2001:db8::1",
      "2001:db8:1::feed",
      "fd00::42",
      "fe80::1234",
      "2606:4700:4700::1111",
  };

  td::IPAddress result;
  auto status = result.init_ipv6_port(kIpv6Pool[static_cast<std::size_t>(rng() % kIpv6Pool.size())], port);
  status.ensure();
  return result;
}

TEST(ConnectionCreatorIpPreferenceLightFuzz, UserPreferenceIsTheOnlyDecisionInputAcrossLargeMatrix) {
  const std::array<std::uint32_t, 4> seed_data{0x20260420u, 0x0A11CEu, 0x00C0FFEEu, 0x0BADC0DEu};
  std::seed_seq seed(seed_data.begin(), seed_data.end());
  std::mt19937_64 rng(seed);

  constexpr std::array<td::int32, 4> kPorts{80, 443, 1080, 8080};

  for ([[maybe_unused]] auto iteration : std::views::iota(0, 10'000)) {
    const bool user_prefer_ipv6 = (rng() & 1u) != 0u;
    const auto port = kPorts[static_cast<std::size_t>(rng() % kPorts.size())];

    td::IPAddress resolved_proxy_ip;
    switch (rng() % 3u) {
      case 0:
        resolved_proxy_ip = random_ipv4(rng, port);
        break;
      case 1:
        resolved_proxy_ip = random_ipv6(rng, port);
        break;
      default:
        resolved_proxy_ip = td::IPAddress();
        break;
    }

    const auto socks5 = td::Proxy::socks5("proxy.example", port, "user", "password");
    const auto http = td::Proxy::http_tcp("proxy.example", port, "user", "password");
    const auto mtproto =
        td::Proxy::mtproto("proxy.example", port, td::mtproto::ProxySecret::from_raw("0123456789abcdef"));

    ASSERT_EQ(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(td::Proxy(), user_prefer_ipv6,
                                                                        resolved_proxy_ip),
              user_prefer_ipv6);
    ASSERT_EQ(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(socks5, user_prefer_ipv6, resolved_proxy_ip),
              user_prefer_ipv6);
    ASSERT_EQ(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(http, user_prefer_ipv6, resolved_proxy_ip),
              user_prefer_ipv6);
    ASSERT_EQ(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(mtproto, user_prefer_ipv6, resolved_proxy_ip),
              user_prefer_ipv6);
  }
}

}  // namespace
