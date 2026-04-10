// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionCreator.h"

#include "td/utils/tests.h"

namespace {

TEST(ConnectionCreatorPingProxySecurity, NullRequestedProxyFallsBackToActiveProxy) {
  auto active_proxy =
      td::Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw("0123456789abcdef"));

  auto effective_proxy = td::ConnectionCreator::resolve_effective_ping_proxy(active_proxy, nullptr);

  ASSERT_EQ(effective_proxy, active_proxy);
}

TEST(ConnectionCreatorPingProxySecurity, ExplicitRequestedProxyOverridesActiveProxy) {
  auto active_proxy = td::Proxy::socks5("active.example", 1080, "user", "password");
  auto requested_proxy =
      td::Proxy::mtproto("requested.example", 443, td::mtproto::ProxySecret::from_raw("fedcba9876543210"));

  auto effective_proxy = td::ConnectionCreator::resolve_effective_ping_proxy(active_proxy, &requested_proxy);

  ASSERT_EQ(effective_proxy, requested_proxy);
}

TEST(ConnectionCreatorPingProxySecurity, NullRequestedProxyWithoutActiveProxyStaysDirect) {
  auto effective_proxy = td::ConnectionCreator::resolve_effective_ping_proxy(td::Proxy(), nullptr);

  ASSERT_FALSE(effective_proxy.use_proxy());
}

}  // namespace