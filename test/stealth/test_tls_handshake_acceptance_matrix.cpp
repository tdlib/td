// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Workstream D1 acceptance matrix:
// verify that the runtime ClientHello for each route lane can complete
// the expected TlsInit handshake path (good response accepted, malformed
// response rejected) under fail-closed route policy.

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/TlsInit.h"

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsInitTestHelpers.h"
#include "test/stealth/TlsInitTestPeer.h"

#include "td/utils/common.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/tests.h"
#include "td/utils/Time.h"

#if !TD_DARWIN

#include "td/utils/port/config.h"

#if TD_PORT_POSIX

namespace {

using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::find_extension;
using td::mtproto::test::make_tls_init_response;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::read_exact;
using td::mtproto::test::TlsInitTestPeer;
using td::mtproto::test::write_all;
using td::mtproto::TlsInit;

constexpr td::uint16 kEchExtType = td::mtproto::test::fixtures::kEchExtensionType;
constexpr td::Slice kFirstResponsePrefix("\x16\x03\x03");
constexpr td::Slice kSecondResponsePrefix("\x17\x03\x03");

class NoopCallback final : public td::TransparentProxy::Callback {
 public:
  void set_result(td::Result<td::BufferedFd<td::SocketFd>>) final {
  }

  void on_connected() final {
  }
};

struct RouteCase final {
  const char *name;
  NetworkRouteHints hints;
  bool expect_ech;
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

td::Status feed_response(TlsInit &tls_init, td::SocketFd &peer_fd, td::Slice response) {
  TRY_STATUS(write_all(peer_fd, response));
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  TRY_RESULT(read_size, TlsInitTestPeer::fd(tls_init).flush_read());
  if (read_size == 0) {
    return td::Status::Error("peer closed before response was consumed");
  }
  return TlsInitTestPeer::wait_hello_response(tls_init);
}

std::array<RouteCase, 3> route_matrix() {
  NetworkRouteHints non_ru;
  non_ru.is_known = true;
  non_ru.is_ru = false;

  NetworkRouteHints ru;
  ru.is_known = true;
  ru.is_ru = true;

  NetworkRouteHints unknown;
  unknown.is_known = false;
  unknown.is_ru = false;

  return {{
      {"non_ru_egress", non_ru, true},
      {"ru_egress", ru, false},
      {"unknown", unknown, false},
  }};
}

TEST(TLS_HandshakeAcceptanceMatrix, RouteLaneMatrixCompletesTlsInitHandshakePath) {
  SKIP_IF_NO_SOCKET_PAIR();

  const auto routes = route_matrix();
  for (const auto &route : routes) {
    for (td::int32 day = 0; day < 16; day++) {
      auto socket_pair = create_socket_pair().move_as_ok();
      const auto unix_time = static_cast<td::int32>((21000 + day) * 86400 + 3600);
      const auto server_time_difference = static_cast<double>(unix_time) - td::Time::now();
      const auto domain = td::string("accept-matrix-") + route.name + '-' + td::to_string(day) + ".example.com";

      TlsInit tls_init(std::move(socket_pair.client), domain, "0123456789secret", td::make_unique<NoopCallback>(), {},
                       server_time_difference, route.hints);
      TlsInitTestPeer::send_hello(tls_init);

      auto wire = flush_client_hello(tls_init, socket_pair.peer);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());

      const bool has_ech = find_extension(parsed.ok_ref(), kEchExtType) != nullptr;
      ASSERT_EQ(route.expect_ech, has_ech);

      auto response = make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init),
                                             kFirstResponsePrefix, kSecondResponsePrefix);
      ASSERT_TRUE(feed_response(tls_init, socket_pair.peer, td::Slice(response)).is_ok());
    }
  }
}

TEST(TLS_HandshakeAcceptanceMatrix, RouteLaneMatrixRejectsMalformedSecondRecordPrefix) {
  SKIP_IF_NO_SOCKET_PAIR();

  const auto routes = route_matrix();
  for (const auto &route : routes) {
    auto socket_pair = create_socket_pair().move_as_ok();
    const auto unix_time = static_cast<td::int32>(1712345678);
    const auto server_time_difference = static_cast<double>(unix_time) - td::Time::now();
    const auto domain = td::string("accept-matrix-malformed-") + route.name + ".example.com";

    TlsInit tls_init(std::move(socket_pair.client), domain, "0123456789secret", td::make_unique<NoopCallback>(), {},
                     server_time_difference, route.hints);
    TlsInitTestPeer::send_hello(tls_init);
    auto wire = flush_client_hello(tls_init, socket_pair.peer);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    auto response = make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init),
                                           kFirstResponsePrefix, td::Slice("\x15\x03\x03"));
    ASSERT_TRUE(feed_response(tls_init, socket_pair.peer, td::Slice(response)).is_error());
  }
}

}  // namespace

#endif  // TD_PORT_POSIX

#endif  // !TD_DARWIN
