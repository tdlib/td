// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::find_extension;
using td::mtproto::test::parse_tls_client_hello;

TEST(TlsSingleLanePolicy, CurrentLanePinsAlpsToChrome133PlusCodepoint) {
  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;

  for (int i = 0; i < 64; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i, route_hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome133Plus) != nullptr);
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome131) == nullptr);
  }
}

TEST(TlsSingleLanePolicy, EchDisabledRoutesKeepSameAlpsCodepointAsCurrentLane) {
  for (bool is_ru : {false, true}) {
    NetworkRouteHints route_hints;
    route_hints.is_known = !is_ru;
    route_hints.is_ru = is_ru;

    for (int i = 0; i < 64; i++) {
      auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i, route_hints);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());

      ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome133Plus) != nullptr);
      ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome131) == nullptr);
    }
  }
}

}  // namespace