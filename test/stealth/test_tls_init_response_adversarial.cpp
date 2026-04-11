// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/actor/actor.h"  // IWYU pragma: keep
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/utils/common.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/port/SocketFd.h"

#include "td/mtproto/TlsInit.h"

#include "test/stealth/TlsInitTestPeer.h"

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/utils/tests.h"

#include "td/utils/port/config.h"

#if TD_PORT_POSIX

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
constexpr td::uint16 kOversizedTlsRecordLength = 16641;

TlsInit create_tls_init(td::SocketFd socket_fd) {
  reset_runtime_ech_failure_state_for_tests();
  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;
  return TlsInit(std::move(socket_fd), "www.google.com", "0123456789secret", td::make_unique<NoopCallback>(), {}, 0.0,
                 route_hints);
}

void overwrite_record_length(td::string &response, size_t offset, td::uint16 record_length) {
  ASSERT_TRUE(offset + 1 < response.size());
  response[offset] = static_cast<char>((record_length >> 8) & 0xFF);
  response[offset + 1] = static_cast<char>(record_length & 0xFF);
}

TEST(TlsInitResponseAdversarial, RejectsOversizedHandshakeRecordLengthWithoutWaitingForMoreBytes) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, kSecondResponsePrefix);
  overwrite_record_length(response, 3, kOversizedTlsRecordLength);

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read().is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_error());
}

TEST(TlsInitResponseAdversarial, RejectsOversizedApplicationDataRecordLengthAfterValidHandshake) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, kSecondResponsePrefix);
  constexpr size_t kSecondRecordLengthOffset = 5 + 2 + 40 + 3;
  overwrite_record_length(response, kSecondRecordLengthOffset, kOversizedTlsRecordLength);

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read().is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_error());
}

TEST(TlsInitResponseAdversarial, RejectsZeroLengthHandshakeRecordEvenWhenHashMatches) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response = make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix,
                                         kSecondResponsePrefix, 0, 32);

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read().is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_error());
}

TEST(TlsInitResponseAdversarial, RejectsZeroLengthApplicationDataRecordEvenWhenHashMatches) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response = make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix,
                                         kSecondResponsePrefix, 48, 0);

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read().is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_error());
}

}  // namespace
#endif  // TD_PORT_POSIX
