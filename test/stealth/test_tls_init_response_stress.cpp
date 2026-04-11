// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/actor/actor.h"  // IWYU pragma: keep
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
constexpr td::uint16 kMaxAcceptedTlsRecordLength = 16640;
constexpr td::uint16 kOversizedTlsRecordLength = 16641;

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
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  TRY_RESULT(read_size, TlsInitTestPeer::fd(tls_init).flush_read(max_read));
  (void)read_size;
  return td::Status::OK();
}

TEST(TlsInitResponseStress, EveryProperPrefixTruncationStaysNonFailingUntilEnoughBytesArrive) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, kSecondResponsePrefix);

  for (size_t cut = 0; cut < response.size(); cut++) {
    auto cut_socket_pair = create_socket_pair().move_as_ok();
    auto cut_tls_init = create_tls_init(std::move(cut_socket_pair.client));
    TlsInitTestPeer::send_hello(cut_tls_init);

    ASSERT_TRUE(
        flush_response_into_tls_init(cut_tls_init, cut_socket_pair.peer, td::Slice(response).substr(0, cut)).is_ok());
    ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(cut_tls_init).is_ok());
  }
}

TEST(TlsInitResponseStress, RepeatedHashCorruptionAlwaysFailsClosed) {
  for (size_t byte_offset = 0; byte_offset < 32; byte_offset++) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client));
    TlsInitTestPeer::send_hello(tls_init);

    auto response =
        make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, kSecondResponsePrefix);
    response[11 + byte_offset] ^= static_cast<char>(1u << (byte_offset % 7));

    ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, response).is_ok());
    ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_error());
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
    TlsInitTestPeer::send_hello(tls_init);

    auto response = make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, prefix);

    ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, response).is_ok());
    ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_error());
  }
}

TEST(TlsInitResponseStress, OversizedLengthClaimsFailClosedImmediately) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, kSecondResponsePrefix);
  response[3] = static_cast<char>(0xFF);
  response[4] = static_cast<char>(0xF0);

  ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, response).is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_error());
}

TEST(TlsInitResponseStress, OversizedLengthHeaderFailsClosedAsSoonAsHeaderIsComplete) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, kSecondResponsePrefix);
  response[3] = static_cast<char>((kOversizedTlsRecordLength >> 8) & 0xFF);
  response[4] = static_cast<char>(kOversizedTlsRecordLength & 0xFF);

  for (size_t cut = 0; cut < 5; cut++) {
    auto cut_socket_pair = create_socket_pair().move_as_ok();
    auto cut_tls_init = create_tls_init(std::move(cut_socket_pair.client));
    TlsInitTestPeer::send_hello(cut_tls_init);

    ASSERT_TRUE(
        flush_response_into_tls_init(cut_tls_init, cut_socket_pair.peer, td::Slice(response).substr(0, cut)).is_ok());
    ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(cut_tls_init).is_ok());
  }

  auto full_header_socket_pair = create_socket_pair().move_as_ok();
  auto full_header_tls_init = create_tls_init(std::move(full_header_socket_pair.client));
  TlsInitTestPeer::send_hello(full_header_tls_init);

  ASSERT_TRUE(
      flush_response_into_tls_init(full_header_tls_init, full_header_socket_pair.peer, td::Slice(response).substr(0, 5))
          .is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(full_header_tls_init).is_error());
}

TEST(TlsInitResponseStress, MaximumAcceptedLengthWaitsForBodyInsteadOfFailing) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, kSecondResponsePrefix);
  response[3] = static_cast<char>((kMaxAcceptedTlsRecordLength >> 8) & 0xFF);
  response[4] = static_cast<char>(kMaxAcceptedTlsRecordLength & 0xFF);

  ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, td::Slice(response).substr(0, 5)).is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_ok());
  ASSERT_FALSE(TlsInitTestPeer::fd(tls_init).input_buffer().empty());
}

TEST(TlsInitResponseStress, ZeroLengthApplicationDataFailsClosedAcrossHandshakeSizeMatrix) {
  for (size_t first_payload_len : {32u, 40u, 48u, 64u}) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client));
    TlsInitTestPeer::send_hello(tls_init);

    auto response = make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix,
                                           kSecondResponsePrefix, first_payload_len, 0);

    ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, response).is_ok());
    ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_error());
  }
}

}  // namespace