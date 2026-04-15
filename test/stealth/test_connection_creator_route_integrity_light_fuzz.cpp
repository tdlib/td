// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionCreator.h"

#include "td/utils/SliceBuilder.h"
#include "td/utils/tests.h"

namespace {

td::IPAddress matrix_ipv4_address(td::uint32 seed, td::int32 port) {
  td::IPAddress result;
  auto third_octet = static_cast<int>((seed * 29u) & 255u);
  auto fourth_octet = static_cast<int>(((seed * 53u) % 254u) + 1u);
  result
      .init_ipv4_port(
          PSTRING() << 198 << '.' << (18 + static_cast<int>(seed % 2u)) << '.' << third_octet << '.' << fourth_octet,
          port)
      .ensure();
  return result;
}

td::IPAddress matrix_ipv6_mapped_address(td::uint32 seed, td::int32 port) {
  td::IPAddress result;
  auto third_octet = static_cast<int>((seed * 29u) & 255u);
  auto fourth_octet = static_cast<int>(((seed * 53u) % 254u) + 1u);
  result
      .init_ipv6_as_ipv4_port(
          PSTRING() << 198 << '.' << (18 + static_cast<int>(seed % 2u)) << '.' << third_octet << '.' << fourth_octet,
          port)
      .ensure();
  return result;
}

TEST(ConnectionCreatorRouteIntegrityLightFuzz, NormalizedMatrixAcceptsEquivalentEndpoints) {
  for (td::uint32 seed = 1; seed <= 256; seed++) {
    auto port = static_cast<td::int32>(4000 + (seed % 200));
    auto status = td::ConnectionCreator::verify_connection_peer(td::Proxy(), matrix_ipv4_address(seed, port),
                                                                matrix_ipv6_mapped_address(seed, port));
    ASSERT_TRUE(status.is_ok());
  }
}

TEST(ConnectionCreatorRouteIntegrityLightFuzz, DriftedMatrixRejectsUnexpectedEndpoints) {
  for (td::uint32 seed = 1; seed <= 256; seed++) {
    auto port = static_cast<td::int32>(5000 + (seed % 200));
    auto status = td::ConnectionCreator::verify_connection_peer(td::Proxy(), matrix_ipv4_address(seed, port),
                                                                matrix_ipv6_mapped_address(seed + 1, port));
    ASSERT_TRUE(status.is_error());
  }
}

}  // namespace