// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/proxy_link_query_pollution_test_utils.h"

namespace {

using td::proxy_link_query_pollution_test::is_unsupported_proxy;

TEST(ProxyLinkQueryPollutionAdversarial, DuplicateProxyServerParameterMustFailClosed) {
  ASSERT_TRUE(is_unsupported_proxy(
      "t.me/proxy?server=google.com&server=example.com&port=80&secret=1234567890abcdef1234567890ABCDEF"));
  ASSERT_TRUE(is_unsupported_proxy(
      "tg:proxy?server=google.com&server=example.com&port=80&secret=1234567890abcdef1234567890ABCDEF"));
}

TEST(ProxyLinkQueryPollutionAdversarial, DuplicateProxyPortParameterMustFailClosed) {
  ASSERT_TRUE(
      is_unsupported_proxy("t.me/proxy?server=google.com&port=80&port=443&secret=1234567890abcdef1234567890ABCDEF"));
  ASSERT_TRUE(
      is_unsupported_proxy("tg:proxy?server=google.com&port=80&port=443&secret=1234567890abcdef1234567890ABCDEF"));
}

TEST(ProxyLinkQueryPollutionAdversarial, DuplicateProxySecretParameterMustFailClosed) {
  ASSERT_TRUE(
      is_unsupported_proxy("t.me/"
                           "proxy?server=google.com&port=80&secret=1234567890abcdef1234567890ABCDEF&secret="
                           "dd1234567890abcdef1234567890ABCDEF"));
  ASSERT_TRUE(
      is_unsupported_proxy("tg:proxy?server=google.com&port=80&secret=1234567890abcdef1234567890ABCDEF&secret="
                           "dd1234567890abcdef1234567890ABCDEF"));
}

TEST(ProxyLinkQueryPollutionAdversarial, DuplicateProxySecretViaEncodedKeyMustFailClosed) {
  ASSERT_TRUE(
      is_unsupported_proxy("t.me/"
                           "proxy?server=google.com&port=80&se%63ret=1234567890abcdef1234567890ABCDEF&secret="
                           "dd1234567890abcdef1234567890ABCDEF"));
  ASSERT_TRUE(
      is_unsupported_proxy("tg:proxy?server=google.com&port=80&se%63ret=1234567890abcdef1234567890ABCDEF&secret="
                           "dd1234567890abcdef1234567890ABCDEF"));
}

TEST(ProxyLinkQueryPollutionAdversarial, DuplicateSocksCredentialsMustFailClosed) {
  ASSERT_TRUE(is_unsupported_proxy("t.me/socks?server=google.com&port=80&user=user&user=dup&pass=pass"));
  ASSERT_TRUE(is_unsupported_proxy("tg:socks?server=google.com&port=80&user=user&pass=pass&pass=dup"));
}

}  // namespace
