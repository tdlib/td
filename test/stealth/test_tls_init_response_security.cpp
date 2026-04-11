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

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::make_tls_init_response;
using td::mtproto::test::TlsInitTestPeer;
using td::mtproto::test::write_all;
using td::mtproto::TlsInit;

class NoopCallback final : public td::TransparentProxy::Callback {
 public:
  void set_result(td::Result<td::BufferedFd<td::SocketFd>>) final {
  }

  void on_connected() final {
  }
};

constexpr td::Slice kFirstResponsePrefix("\x16\x03\x03");
constexpr td::Slice kSecondResponsePrefix("\x14\x03\x03\x00\x01\x01\x17\x03\x03");

TlsInit create_tls_init(td::SocketFd socket_fd) {
  reset_runtime_ech_failure_state_for_tests();
  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;
  return TlsInit(std::move(socket_fd), "www.google.com", "0123456789secret", td::make_unique<NoopCallback>(), {}, 0.0,
                 route_hints);
}

TEST(TlsInitResponseSecurity, IncompleteValidResponseDoesNotFailPrematurely) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, kSecondResponsePrefix);
  auto split_pos = response.size() - 3;

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read(split_pos).is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_ok());
  ASSERT_FALSE(TlsInitTestPeer::fd(tls_init).input_buffer().empty());
}

TEST(TlsInitResponseSecurity, RejectsWrongResponseHashEvenWithValidTlsLikeLayout) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, kSecondResponsePrefix);
  response[11] ^= 0x01;

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read().is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_error());
}

TEST(TlsInitResponseSecurity, RejectsWrongSecondRecordPrefixBeforeHashValidation) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response = make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix,
                                         "\x14\x03\x03\x00\x01\x01\x16\x03\x03");

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read().is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_error());
}

TEST(TlsInitResponseSecurity, AcceptsHandshakeThenApplicationDataWithoutChangeCipherSpec) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, "\x17\x03\x03");

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read().is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_ok());
}

TEST(TlsInitResponseSecurity, RejectsAlertRecordAfterHandshakeEvenWhenHashMatches) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, "\x15\x03\x03");

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read().is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_error());
}

}  // namespace