// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/telegram/net/ConnectionRetryPolicy.h"

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/mtproto/TlsInit.h"

#include "td/net/ProxySetupError.h"

#include "test/stealth/TlsInitTestHelpers.h"
#include "test/stealth/TlsInitTestPeer.h"

#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/port/PollFlags.h"

#include "td/utils/port/config.h"

#if TD_PORT_POSIX

namespace td {
namespace test {

enum class ProxyRejectScenario {
  ImmediateClose,
  MalformedTlsResponse,
  WrongRegimeHttpResponse,
  WrongRegimeSocksResponse,
};

class NoopTransparentProxyCallback final : public TransparentProxy::Callback {
 public:
  void set_result(Result<BufferedFd<SocketFd>>) final {
  }

  void on_connected() final {
  }
};

inline mtproto::TlsInit create_tls_proxy_rejection_probe(SocketFd socket_fd) {
  mtproto::stealth::reset_runtime_ech_failure_state_for_tests();

  mtproto::stealth::NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;

  return mtproto::TlsInit(std::move(socket_fd), "www.google.com", "0123456789secret",
                          make_unique<NoopTransparentProxyCallback>(), {}, 0.0, route_hints);
}

inline Status run_tls_proxy_rejection_scenario(ProxyRejectScenario scenario) {
  auto socket_pair = mtproto::test::create_socket_pair().move_as_ok();
  auto tls_init = create_tls_proxy_rejection_probe(std::move(socket_pair.client));
  mtproto::test::TlsInitTestPeer::send_hello(tls_init);

  switch (scenario) {
    case ProxyRejectScenario::ImmediateClose: {
      socket_pair.peer.close();
      auto &fd = mtproto::test::TlsInitTestPeer::fd(tls_init);
      fd.get_poll_info().add_flags(PollFlags::Read() | PollFlags::Close());
      auto flush_status = fd.flush_read();
      if (flush_status.is_error()) {
        return flush_status.move_as_error();
      }
      return can_close_local(fd) ? make_proxy_setup_error(ProxySetupErrorCode::ConnectionClosed, "Connection closed")
                                 : Status::OK();
    }
    case ProxyRejectScenario::MalformedTlsResponse: {
      auto response = mtproto::test::make_tls_init_response("0123456789secret",
                                                            mtproto::test::TlsInitTestPeer::hello_rand(tls_init),
                                                            "\x16\x03\x03", "\x14\x03\x03\x00\x01\x01\x17\x03\x03");
      response[3] = static_cast<char>(0x41);
      response[4] = static_cast<char>(0x01);
      auto write_status = mtproto::test::write_all(socket_pair.peer, response);
      if (write_status.is_error()) {
        return write_status;
      }
      auto &fd = mtproto::test::TlsInitTestPeer::fd(tls_init);
      fd.get_poll_info().add_flags(PollFlags::Read());
      auto flush_status = fd.flush_read();
      if (flush_status.is_error()) {
        return flush_status.move_as_error();
      }
      return mtproto::test::TlsInitTestPeer::wait_hello_response(tls_init);
    }
    case ProxyRejectScenario::WrongRegimeHttpResponse: {
      auto write_status = mtproto::test::write_all(socket_pair.peer, "HTTP/1.1 200 OK\r\n\r\n");
      if (write_status.is_error()) {
        return write_status;
      }
      auto &fd = mtproto::test::TlsInitTestPeer::fd(tls_init);
      fd.get_poll_info().add_flags(PollFlags::Read());
      auto flush_status = fd.flush_read();
      if (flush_status.is_error()) {
        return flush_status.move_as_error();
      }
      return mtproto::test::TlsInitTestPeer::wait_hello_response(tls_init);
    }
    case ProxyRejectScenario::WrongRegimeSocksResponse: {
      auto write_status = mtproto::test::write_all(socket_pair.peer, Slice("\x05\x00\x00\x01\x00", 5));
      if (write_status.is_error()) {
        return write_status;
      }
      auto &fd = mtproto::test::TlsInitTestPeer::fd(tls_init);
      fd.get_poll_info().add_flags(PollFlags::Read());
      auto flush_status = fd.flush_read();
      if (flush_status.is_error()) {
        return flush_status.move_as_error();
      }
      return mtproto::test::TlsInitTestPeer::wait_hello_response(tls_init);
    }
  }

  UNREACHABLE();
}

inline vector<int32> collect_retry_delays(ConnectionFailureBackoff backoff, int32 attempts) {
  vector<int32> delays;
  delays.reserve(attempts);

  int32 now = 1000;
  for (int32 i = 0; i < attempts; i++) {
    backoff.add_event(now);
    delays.push_back(backoff.get_wakeup_at() - now);
    now = backoff.get_wakeup_at();
  }
  return delays;
}

}  // namespace test
}  // namespace td

#endif  // TD_PORT_POSIX