// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/actor/actor.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/utils/common.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"

#include "td/mtproto/TlsInit.h"

#include "test/stealth/TlsInitTestPeer.h"
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: GPL-3.0-only
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsInitTestHelpers.h"

#include "td/utils/tests.h"

#include <unordered_set>

#include "td/utils/port/config.h"

#if TD_PORT_POSIX

namespace {

using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::find_extension;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::read_exact;
using td::mtproto::test::TlsInitTestPeer;
using td::mtproto::TlsInit;

class NoopCallback final : public td::TransparentProxy::Callback {
 public:
  void set_result(td::Result<td::BufferedFd<td::SocketFd>>) final {
  }

  void on_connected() final {
  }
};

td::string flush_client_hello(TlsInit &tls_init, td::SocketFd &peer_fd) {
  auto bytes_to_read = TlsInitTestPeer::fd(tls_init).ready_for_flush_write();
  CHECK(bytes_to_read > 0);
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Write());
  while (TlsInitTestPeer::fd(tls_init).ready_for_flush_write() > 0) {
    auto flush_status = TlsInitTestPeer::fd(tls_init).flush_write();
    CHECK(flush_status.is_ok());
  }
  return read_exact(peer_fd, bytes_to_read).move_as_ok();
}

bool has_ech_extension(const td::mtproto::test::ParsedClientHello &hello) {
  return find_extension(hello, td::mtproto::test::fixtures::kEchExtensionType) != nullptr;
}

TEST(TlsInitRouteStress, RoutePolicyRemainsStableAcrossManyTlsInitInstances) {
  SKIP_IF_NO_SOCKET_PAIR();
  reset_runtime_ech_failure_state_for_tests();
  struct Scenario final {
    const char *name;
    NetworkRouteHints route_hints;
    bool expect_ech;
  };

  const Scenario scenarios[] = {
      {"unknown", NetworkRouteHints{}, false},
      {"ru", NetworkRouteHints{true, true}, false},
      {"known_non_ru", NetworkRouteHints{true, false}, true},
  };

  for (const auto &scenario : scenarios) {
    std::unordered_set<td::string> hello_randoms;

    for (int i = 0; i < 256; i++) {
      auto socket_pair = create_socket_pair().move_as_ok();
      TlsInit tls_init(std::move(socket_pair.client), "www.google.com", "0123456789secret",
                       td::make_unique<NoopCallback>(), {}, 0.0, scenario.route_hints);
      TlsInitTestPeer::send_hello(tls_init);

      auto wire = flush_client_hello(tls_init, socket_pair.peer);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
      ASSERT_EQ(scenario.expect_ech, has_ech_extension(parsed.ok()));
      ASSERT_TRUE(find_extension(parsed.ok(), 0xFE02) == nullptr);
      ASSERT_EQ(wire.substr(11, 32), TlsInitTestPeer::hello_rand(tls_init));

      hello_randoms.insert(TlsInitTestPeer::hello_rand(tls_init));
    }

    ASSERT_TRUE(hello_randoms.size() > 32u);
  }
}

TEST(TlsInitRouteStress, RuntimeRoutePolicyPreservesCountryDerivedFailClosedSemantics) {
  SKIP_IF_NO_SOCKET_PAIR();
  reset_runtime_ech_failure_state_for_tests();
  const td::Slice country_codes[] = {"", "R1", "RU", "ru", "US", "de"};

  for (auto country_code : country_codes) {
    auto route_hints = td::mtproto::stealth::route_hints_from_country_code(country_code);
    auto socket_pair = create_socket_pair().move_as_ok();
    TlsInit tls_init(std::move(socket_pair.client), "www.google.com", "0123456789secret",
                     td::make_unique<NoopCallback>(), {}, 0.0, route_hints);
    TlsInitTestPeer::send_hello(tls_init);

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    auto expect_ech = route_hints.is_known && !route_hints.is_ru;
    ASSERT_EQ(expect_ech, has_ech_extension(parsed.ok()));
  }
}

}  // namespace
#endif  // TD_PORT_POSIX
