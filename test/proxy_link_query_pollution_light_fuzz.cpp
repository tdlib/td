// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/proxy_link_query_pollution_test_utils.h"

namespace {

using td::proxy_link_query_pollution_test::is_unsupported_proxy;

td::string build_proxy_pollution_case(td::Slice scheme, td::Slice key, td::Slice first_value, td::Slice second_value) {
  return PSTRING() << scheme << "?server=google.com&port=80&secret=1234567890abcdef1234567890ABCDEF&" << key << "="
                   << first_value << "&" << key << "=" << second_value;
}

TEST(ProxyLinkQueryPollutionLightFuzz, DuplicateProxyKeyFamiliesAlwaysFailClosed) {
  const td::vector<td::string> schemes = {"t.me/proxy", "tg:proxy"};
  const td::vector<td::string> keys = {"server", "port", "secret", "se%63ret"};

  for (const auto &scheme : schemes) {
    for (const auto &key : keys) {
      for (int i = 0; i < 128; i++) {
        td::string first_value;
        td::string second_value;
        switch (i % 4) {
          case 0:
            first_value = "google.com";
            second_value = "example.com";
            break;
          case 1:
            first_value = "80";
            second_value = "443";
            break;
          case 2:
            first_value = "1234567890abcdef1234567890ABCDEF";
            second_value = "dd1234567890abcdef1234567890ABCDEF";
            break;
          default:
            first_value = "de1234567890abcdef1234567890ABCDEF";
            second_value = "1234567890abcdef1234567890ABCDEF";
            break;
        }

        auto polluted = build_proxy_pollution_case(scheme, key, first_value, second_value);
        ASSERT_TRUE(is_unsupported_proxy(polluted));
      }
    }
  }
}

}  // namespace
