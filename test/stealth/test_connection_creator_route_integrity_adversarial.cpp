// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionCreator.h"

#include "td/utils/tests.h"

namespace {

td::IPAddress ipv4_address(td::CSlice ip, td::int32 port) {
  td::IPAddress result;
  result.init_ipv4_port(ip, port).ensure();
  return result;
}

td::IPAddress ipv6_address(td::CSlice ip, td::int32 port) {
  td::IPAddress result;
  result.init_ipv6_port(ip, port).ensure();
  return result;
}

TEST(ConnectionCreatorRouteIntegrityAdversarial, DirectIpv4EndpointMismatchFailsClosed) {
  ASSERT_TRUE(td::ConnectionCreator::verify_connection_peer(td::Proxy(), ipv4_address("149.154.167.50", 443),
                                                            ipv4_address("149.154.167.51", 443))
                  .is_error());
}

TEST(ConnectionCreatorRouteIntegrityAdversarial, DirectPortMismatchFailsClosed) {
  ASSERT_TRUE(td::ConnectionCreator::verify_connection_peer(td::Proxy(), ipv4_address("149.154.167.50", 443),
                                                            ipv4_address("149.154.167.50", 80))
                  .is_error());
}

TEST(ConnectionCreatorRouteIntegrityAdversarial, MissingExpectedEndpointFailsClosed) {
  ASSERT_TRUE(
      td::ConnectionCreator::verify_connection_peer(td::Proxy(), td::IPAddress(), ipv4_address("149.154.167.50", 443))
          .is_error());
}

TEST(ConnectionCreatorRouteIntegrityAdversarial, MissingObservedEndpointFailsClosed) {
  ASSERT_TRUE(
      td::ConnectionCreator::verify_connection_peer(td::Proxy(), ipv4_address("149.154.167.50", 443), td::IPAddress())
          .is_error());
}

TEST(ConnectionCreatorRouteIntegrityAdversarial, UnmappedCrossFamilyEndpointFailsClosed) {
  ASSERT_TRUE(td::ConnectionCreator::verify_connection_peer(td::Proxy(), ipv4_address("149.154.167.50", 443),
                                                            ipv6_address("2001:67c:4e8:f002::a", 443))
                  .is_error());
}

}  // namespace