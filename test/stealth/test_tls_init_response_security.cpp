//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/actor.h"
#include "td/mtproto/stealth/Interfaces.h"
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

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::make_tls_init_response;
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
  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;
  return TlsInit(std::move(socket_fd), "www.google.com", "0123456789secret", td::make_unique<NoopCallback>(), {}, 0.0,
                 route_hints);
}

TEST(TlsInitResponseSecurity, IncompleteValidResponseDoesNotFailPrematurely) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  tls_init.send_hello();

  auto response =
      make_tls_init_response("0123456789secret", tls_init.hello_rand_, kFirstResponsePrefix, kSecondResponsePrefix);
  auto split_pos = response.size() - 3;

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  tls_init.fd_.get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(tls_init.fd_.flush_read(split_pos).is_ok());
  ASSERT_TRUE(tls_init.wait_hello_response().is_ok());
  ASSERT_FALSE(tls_init.fd_.input_buffer().empty());
}

TEST(TlsInitResponseSecurity, RejectsWrongResponseHashEvenWithValidTlsLikeLayout) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  tls_init.send_hello();

  auto response =
      make_tls_init_response("0123456789secret", tls_init.hello_rand_, kFirstResponsePrefix, kSecondResponsePrefix);
  response[11] ^= 0x01;

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  tls_init.fd_.get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(tls_init.fd_.flush_read().is_ok());
  ASSERT_TRUE(tls_init.wait_hello_response().is_error());
}

TEST(TlsInitResponseSecurity, RejectsWrongSecondRecordPrefixBeforeHashValidation) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  tls_init.send_hello();

  auto response = make_tls_init_response("0123456789secret", tls_init.hello_rand_, kFirstResponsePrefix,
                                         "\x14\x03\x03\x00\x01\x01\x16\x03\x03");

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  tls_init.fd_.get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(tls_init.fd_.flush_read().is_ok());
  ASSERT_TRUE(tls_init.wait_hello_response().is_error());
}

TEST(TlsInitResponseSecurity, RejectsAlternativeSingleRecordLayoutEvenWhenHashMatches) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  tls_init.send_hello();

  auto response =
      make_tls_init_response("0123456789secret", tls_init.hello_rand_, kFirstResponsePrefix, "\x17\x03\x03");

  ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
  tls_init.fd_.get_poll_info().add_flags(td::PollFlags::Read());
  ASSERT_TRUE(tls_init.fd_.flush_read().is_ok());
  ASSERT_TRUE(tls_init.wait_hello_response().is_error());
}

}  // namespace