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
#include "td/utils/Status.h"

#include "td/mtproto/TlsInit.h"

#include "test/stealth/TlsInitTestPeer.h"

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/utils/tests.h"

#include "td/utils/port/config.h"

#if TD_PORT_POSIX

namespace {

using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::test::append_u16_be;
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

td::Status flush_response_into_tls_init(TlsInit &tls_init, td::SocketFd &peer_fd, td::Slice response) {
  TRY_STATUS(write_all(peer_fd, response));
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  TRY_RESULT(read_size, TlsInitTestPeer::fd(tls_init).flush_read());
  (void)read_size;
  return td::Status::OK();
}

td::string make_tls_application_data_record(td::Slice payload) {
  td::string record("\x17\x03\x03", 3);
  append_u16_be(record, static_cast<td::uint16>(payload.size()));
  record += payload.str();
  return record;
}

TEST(TlsInitResponseMultiRecordIntegration,
     SuccessfulHelloResponsePreservesCaptureBackedMultiRecordApplicationDataSuffix) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client));
  TlsInitTestPeer::send_hello(tls_init);

  // The checked-in ServerHello corpus pins a browser-capture handshake layout of
  // Handshake(22) then ChangeCipherSpec(20). We validate that once the synthetic
  // emulate_tls response completes, additional encrypted ApplicationData records
  // arriving in the same read burst remain untouched for the next layer.
  auto response =
      make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix, kSecondResponsePrefix);
  const td::string trailing_suffix = make_tls_application_data_record("\x99\x98\x97\x96") +
                                     make_tls_application_data_record("\x55\x44\x33\x22\x11\x00");
  response += trailing_suffix;

  ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, response).is_ok());
  ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_ok());
  ASSERT_EQ(trailing_suffix.size(), TlsInitTestPeer::fd(tls_init).input_buffer().size());

  auto buffered = TlsInitTestPeer::fd(tls_init).input_buffer().clone().move_as_buffer_slice().as_slice().str();
  ASSERT_EQ(trailing_suffix, buffered);
}

}  // namespace
#endif  // TD_PORT_POSIX
