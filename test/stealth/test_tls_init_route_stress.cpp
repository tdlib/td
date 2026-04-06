//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/actor.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/utils/common.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"

#define private public
#define protected public
#include "td/mtproto/TlsInit.h"
#undef protected
#undef private

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsInitTestHelpers.h"

#include "td/utils/tests.h"

#include <unordered_map>
#include <unordered_set>

namespace {

using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::find_extension;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::read_exact;
using td::mtproto::TlsInit;

class NoopCallback final : public td::TransparentProxy::Callback {
 public:
  void set_result(td::Result<td::BufferedFd<td::SocketFd>>) final {
  }

  void on_connected() final {
  }
};

td::string flush_client_hello(TlsInit &tls_init, td::SocketFd &peer_fd) {
  auto bytes_to_read = tls_init.fd_.ready_for_flush_write();
  CHECK(bytes_to_read > 0);
  tls_init.fd_.get_poll_info().add_flags(td::PollFlags::Write());
  while (tls_init.fd_.ready_for_flush_write() > 0) {
    auto flush_status = tls_init.fd_.flush_write();
    CHECK(flush_status.is_ok());
  }
  return read_exact(peer_fd, bytes_to_read).move_as_ok();
}

bool has_ech_extension(const td::mtproto::test::ParsedClientHello &hello) {
  return find_extension(hello, td::mtproto::test::fixtures::kEchExtensionType) != nullptr;
}

TEST(TlsInitRouteStress, RoutePolicyRemainsStableAcrossManyTlsInitInstances) {
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
    std::unordered_map<size_t, int> wire_lengths;

    for (int i = 0; i < 256; i++) {
      auto socket_pair = create_socket_pair().move_as_ok();
      TlsInit tls_init(std::move(socket_pair.client), "www.google.com", "0123456789secret",
                       td::make_unique<NoopCallback>(), {}, 0.0, scenario.route_hints);
      tls_init.send_hello();

      auto wire = flush_client_hello(tls_init, socket_pair.peer);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
      ASSERT_EQ(scenario.expect_ech, has_ech_extension(parsed.ok()));
      ASSERT_TRUE(find_extension(parsed.ok(), 0xFE02) == nullptr);
      ASSERT_EQ(wire.substr(11, 32), tls_init.hello_rand_);

      hello_randoms.insert(tls_init.hello_rand_);
      wire_lengths[wire.size()]++;
    }

    ASSERT_TRUE(hello_randoms.size() > 32u);
    ASSERT_TRUE(wire_lengths.size() > 1u);
  }
}

TEST(TlsInitRouteStress, RuntimeRoutePolicyPreservesCountryDerivedFailClosedSemantics) {
  reset_runtime_ech_failure_state_for_tests();
  const td::Slice country_codes[] = {"", "R1", "RU", "ru", "US", "de"};

  for (auto country_code : country_codes) {
    auto route_hints = td::mtproto::stealth::route_hints_from_country_code(country_code);
    auto socket_pair = create_socket_pair().move_as_ok();
    TlsInit tls_init(std::move(socket_pair.client), "www.google.com", "0123456789secret",
                     td::make_unique<NoopCallback>(), {}, 0.0, route_hints);
    tls_init.send_hello();

    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    auto expect_ech = route_hints.is_known && !route_hints.is_ru;
    ASSERT_EQ(expect_ech, has_ech_extension(parsed.ok()));
  }
}

}  // namespace