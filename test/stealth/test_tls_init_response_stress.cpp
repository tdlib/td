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

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
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
  reset_runtime_ech_failure_state_for_tests();
  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;
  return TlsInit(std::move(socket_fd), "www.google.com", "0123456789secret", td::make_unique<NoopCallback>(), {}, 0.0,
                 route_hints);
}

td::Status flush_response_into_tls_init(TlsInit &tls_init, td::SocketFd &peer_fd, td::Slice response,
                                        size_t max_read = std::numeric_limits<size_t>::max()) {
  TRY_STATUS(write_all(peer_fd, response));
  tls_init.fd_.get_poll_info().add_flags(td::PollFlags::Read());
  TRY_RESULT(read_size, tls_init.fd_.flush_read(max_read));
  (void)read_size;
  return td::Status::OK();
}

TEST(TlsInitResponseStress, EveryProperPrefixTruncationStaysNonFailingUntilEnoughBytesArrive) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  tls_init.send_hello();

  auto response =
      make_tls_init_response("0123456789secret", tls_init.hello_rand_, kFirstResponsePrefix, kSecondResponsePrefix);

  for (size_t cut = 0; cut < response.size(); cut++) {
    auto cut_socket_pair = create_socket_pair().move_as_ok();
    auto cut_tls_init = create_tls_init(std::move(cut_socket_pair.client));
    cut_tls_init.send_hello();

    ASSERT_TRUE(
        flush_response_into_tls_init(cut_tls_init, cut_socket_pair.peer, td::Slice(response).substr(0, cut)).is_ok());
    ASSERT_TRUE(cut_tls_init.wait_hello_response().is_ok());
  }
}

TEST(TlsInitResponseStress, RepeatedHashCorruptionAlwaysFailsClosed) {
  for (size_t byte_offset = 0; byte_offset < 32; byte_offset++) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client));
    tls_init.send_hello();

    auto response =
        make_tls_init_response("0123456789secret", tls_init.hello_rand_, kFirstResponsePrefix, kSecondResponsePrefix);
    response[11 + byte_offset] ^= static_cast<char>(1u << (byte_offset % 7));

    ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, response).is_ok());
    ASSERT_TRUE(tls_init.wait_hello_response().is_error());
  }
}

TEST(TlsInitResponseStress, NearMissSecondRecordPrefixesAlwaysFailClosed) {
  const td::string prefixes[] = {
      "\x14\x03\x03\x00\x01\x00\x17\x03\x03", "\x14\x03\x01\x00\x01\x01\x17\x03\x03",
      "\x15\x03\x03\x00\x01\x01\x17\x03\x03", "\x14\x03\x03\x00\x01\x01\x17\x03\x04",
      "\x14\x03\x03\x00\x02\x01\x17\x03\x03",
  };

  for (const auto &prefix : prefixes) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client));
    tls_init.send_hello();

    auto response = make_tls_init_response("0123456789secret", tls_init.hello_rand_, kFirstResponsePrefix, prefix);

    ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, response).is_ok());
    ASSERT_TRUE(tls_init.wait_hello_response().is_error());
  }
}

TEST(TlsInitResponseStress, OversizedLengthClaimsDoNotTriggerPrematureErrors) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  tls_init.send_hello();

  auto response =
      make_tls_init_response("0123456789secret", tls_init.hello_rand_, kFirstResponsePrefix, kSecondResponsePrefix);
  response[3] = static_cast<char>(0xFF);
  response[4] = static_cast<char>(0xF0);

  ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, response).is_ok());
  ASSERT_TRUE(tls_init.wait_hello_response().is_ok());
  ASSERT_FALSE(tls_init.fd_.input_buffer().empty());
}

}  // namespace