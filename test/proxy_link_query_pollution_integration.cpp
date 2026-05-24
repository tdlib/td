// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/proxy_link_query_pollution_test_utils.h"

namespace {

using td::proxy_link_query_pollution_test::is_unsupported_proxy;

TEST(ProxyLinkQueryPollutionIntegration, ValidThenInvalidProxySecretStillFailsClosed) {
  ASSERT_TRUE(
      is_unsupported_proxy("t.me/"
                           "proxy?server=google.com&port=80&secret=1234567890abcdef1234567890ABCDEF&secret="
                           "de1234567890abcdef1234567890ABCDEF"));
  ASSERT_TRUE(
      is_unsupported_proxy("tg:proxy?server=google.com&port=80&secret=1234567890abcdef1234567890ABCDEF&secret="
                           "de1234567890abcdef1234567890ABCDEF"));
}

TEST(ProxyLinkQueryPollutionIntegration, InvalidThenValidProxySecretStillFailsClosed) {
  ASSERT_TRUE(
      is_unsupported_proxy("t.me/"
                           "proxy?server=google.com&port=80&secret=de1234567890abcdef1234567890ABCDEF&secret="
                           "1234567890abcdef1234567890ABCDEF"));
  ASSERT_TRUE(
      is_unsupported_proxy("tg:proxy?server=google.com&port=80&secret=de1234567890abcdef1234567890ABCDEF&secret="
                           "1234567890abcdef1234567890ABCDEF"));
}

TEST(ProxyLinkQueryPollutionIntegration, DuplicateSocksServerOrPortNeverDowngradesToSupportedProxy) {
  ASSERT_TRUE(is_unsupported_proxy("t.me/socks?server=google.com&server=example.com&port=80&user=user&pass=pass"));
  ASSERT_TRUE(is_unsupported_proxy("tg:socks?server=google.com&port=80&port=1080&user=user&pass=pass"));
}

}  // namespace
