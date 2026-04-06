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
#include "td/utils/Slice.h"
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

TEST(TlsInitIntegration, KnownNonRuRouteSendsEchEnabledClientHello) {
  reset_runtime_ech_failure_state_for_tests();
  auto socket_pair = create_socket_pair().move_as_ok();

  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;

  TlsInit tls_init(std::move(socket_pair.client), "www.google.com", "0123456789secret", td::make_unique<NoopCallback>(),
                   {}, 0.0, route_hints);
  tls_init.send_hello();

  auto wire = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);
  ASSERT_EQ(wire.substr(11, 32), tls_init.hello_rand_);
}

TEST(TlsInitIntegration, RuRouteSendsEchDisabledClientHello) {
  reset_runtime_ech_failure_state_for_tests();
  auto socket_pair = create_socket_pair().move_as_ok();

  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = true;

  TlsInit tls_init(std::move(socket_pair.client), "www.google.com", "0123456789secret", td::make_unique<NoopCallback>(),
                   {}, 0.0, route_hints);
  tls_init.send_hello();

  auto wire = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
}

TEST(TlsInitIntegration, UnknownRouteDefaultsToEchDisabledClientHello) {
  reset_runtime_ech_failure_state_for_tests();
  auto socket_pair = create_socket_pair().move_as_ok();

  TlsInit tls_init(std::move(socket_pair.client), "www.google.com", "0123456789secret", td::make_unique<NoopCallback>(),
                   {}, 0.0);
  tls_init.send_hello();

  auto wire = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
}

}  // namespace