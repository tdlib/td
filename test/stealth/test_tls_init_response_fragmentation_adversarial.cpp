// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/actor/actor.h"  // IWYU pragma: keep
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/net/ProxySetupError.h"
#include "td/utils/common.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/port/SocketFd.h"

#include "td/mtproto/TlsInit.h"

#include "test/stealth/TlsInitTestHelpers.h"
#include "test/stealth/TlsInitTestPeer.h"

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
constexpr std::size_t kResponseRandomOffset = 11;
constexpr std::size_t kResponseRandomSize = 32;

TlsInit create_tls_init(td::SocketFd socket_fd) {
  reset_runtime_ech_failure_state_for_tests();
  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;
  return TlsInit(std::move(socket_fd), "www.google.com", "0123456789secret", td::make_unique<NoopCallback>(), {}, 0.0,
                 route_hints);
}

TEST(TlsInitResponseFragmentationAdversarial, TamperedHashRemainsHashMismatchAcrossFragmentBoundaries) {
  constexpr td::int32 kFragmentationTrials = static_cast<td::int32>(kResponseRandomSize * 8);

  for (td::int32 trial = 0; trial < kFragmentationTrials; ++trial) {
    SKIP_IF_NO_SOCKET_PAIR();
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client));
    TlsInitTestPeer::send_hello(tls_init);

    auto response = make_tls_init_response("0123456789secret", TlsInitTestPeer::hello_rand(tls_init),
                                           kFirstResponsePrefix, kSecondResponsePrefix);
    const auto tamper_bit_index = static_cast<std::size_t>(trial);
    const auto tamper_byte = kResponseRandomOffset + (tamper_bit_index / 8);
    const auto tamper_mask = static_cast<char>(1u << (tamper_bit_index % 8));
    response[tamper_byte] ^= tamper_mask;

    const auto split_pos = static_cast<size_t>(1 + (trial % static_cast<td::int32>(response.size() - 1)));

    ASSERT_TRUE(write_all(socket_pair.peer, response).is_ok());
    TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
    ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read(split_pos).is_ok());
    ASSERT_TRUE(TlsInitTestPeer::wait_hello_response(tls_init).is_ok());
    ASSERT_FALSE(TlsInitTestPeer::fd(tls_init).input_buffer().empty());

    TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
    ASSERT_TRUE(TlsInitTestPeer::fd(tls_init).flush_read().is_ok());

    auto status = TlsInitTestPeer::wait_hello_response(tls_init);
    ASSERT_TRUE(status.is_error());
    ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloResponseHashMismatch), status.code());
  }
}

}  // namespace

#endif  // TD_PORT_POSIX
