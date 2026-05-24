// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/proxy_link_query_pollution_test_utils.h"

namespace {

using td::proxy_link_query_pollution_test::is_unsupported_proxy;

TEST(ProxyLinkQueryPollutionStress, RepeatedAmbiguousProxyLinksRemainFailClosedAndDeterministic) {
  const td::vector<td::string> polluted_links = {
      "t.me/proxy?server=google.com&server=example.com&port=80&secret=1234567890abcdef1234567890ABCDEF",
      "tg:proxy?server=google.com&port=80&port=443&secret=1234567890abcdef1234567890ABCDEF",
      "t.me/"
      "proxy?server=google.com&port=80&secret=1234567890abcdef1234567890ABCDEF&secret="
      "dd1234567890abcdef1234567890ABCDEF",
      "tg:proxy?server=google.com&port=80&se%63ret=1234567890abcdef1234567890ABCDEF&secret="
      "dd1234567890abcdef1234567890ABCDEF",
      "t.me/socks?server=google.com&port=80&user=user&user=dup&pass=pass",
      "tg:socks?server=google.com&port=80&user=user&pass=pass&pass=dup",
  };

  for (int iteration = 0; iteration < 3000; iteration++) {
    for (const auto &url : polluted_links) {
      ASSERT_TRUE(is_unsupported_proxy(url));
    }
  }
}

}  // namespace
