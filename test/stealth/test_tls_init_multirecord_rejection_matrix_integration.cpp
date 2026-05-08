// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

#include "td/actor/actor.h"  // IWYU pragma: keep
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/net/ProxySetupError.h"
#include "td/telegram/net/ConnectionRetryPolicy.h"
#include "td/utils/common.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Status.h"

#include "td/mtproto/TlsInit.h"

#include "test/stealth/ProxyRejectionTestHarness.h"
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

TlsInit create_tls_init(td::SocketFd socket_fd) {
  reset_runtime_ech_failure_state_for_tests();
  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;
  return TlsInit(std::move(socket_fd), "www.google.com", "0123456789secret", td::make_unique<NoopCallback>(), {}, 0.0,
                 route_hints);
}

td::Proxy tls_proxy() {
  td::string raw_secret;
  raw_secret.push_back(static_cast<char>(0xee));
  raw_secret += "0123456789abcdefdomain";
  return td::Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw(raw_secret));
}

td::Status flush_response_into_tls_init(TlsInit &tls_init, td::SocketFd &peer_fd, td::Slice response,
                                        size_t max_read = std::numeric_limits<size_t>::max()) {
  TRY_STATUS(write_all(peer_fd, response));
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  TRY_RESULT(read_size, TlsInitTestPeer::fd(tls_init).flush_read(max_read));
  (void)read_size;
  return td::Status::OK();
}

void assert_fail_closed_backoff() {
  td::ConnectionFailureBackoff backoff;
  auto delays = td::test::collect_retry_delays(std::move(backoff), 4);
  ASSERT_EQ(static_cast<size_t>(4), delays.size());
  ASSERT_EQ(1, delays[0]);
  for (size_t i = 1; i < delays.size(); i++) {
    ASSERT_TRUE(delays[i] >= delays[i - 1]);
    ASSERT_TRUE(delays[i] <= td::ConnectionFailureBackoff::max_backoff_seconds());
  }
}

TEST(TlsInitMultiRecordRejectionMatrixIntegration, MalformedAndHashMismatchResponsesMapToTypedFailClosedRetryPaths) {
  struct ScenarioCase final {
    const char *name;
    td::string (*make_response)(const std::string &hello_rand);
    td::int32 expected_code;
    td::ProxyFailureReason expected_reason;
  };

  const ScenarioCase cases[] = {
      {"zero_length_application_data",
       [](const std::string &hello_rand) {
         return make_tls_init_response("0123456789secret", hello_rand, kFirstResponsePrefix, kSecondResponsePrefix, 40,
                                       0);
       },
       static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse),
       td::ProxyFailureReason::MalformedResponse},
      {"invalid_second_record_prefix",
       [](const std::string &hello_rand) {
         return make_tls_init_response("0123456789secret", hello_rand, kFirstResponsePrefix,
                                       "\x14\x03\x03\x00\x02\x01\x17\x03\x03");
       },
       static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse),
       td::ProxyFailureReason::MalformedResponse},
      {"oversized_length_claim",
       [](const std::string &hello_rand) {
         auto response =
             make_tls_init_response("0123456789secret", hello_rand, kFirstResponsePrefix, kSecondResponsePrefix);
         response[3] = static_cast<char>(0xFF);
         response[4] = static_cast<char>(0xF0);
         return response;
       },
       static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse),
       td::ProxyFailureReason::MalformedResponse},
      {"response_hash_mismatch",
       [](const std::string &hello_rand) {
         auto response =
             make_tls_init_response("0123456789secret", hello_rand, kFirstResponsePrefix, kSecondResponsePrefix);
         response[11] ^= 0x01;
         return response;
       },
       static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloResponseHashMismatch),
       td::ProxyFailureReason::ResponseHashMismatch},
  };

  for (const auto &test_case : cases) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client));
    TlsInitTestPeer::send_hello(tls_init);

    auto response = test_case.make_response(TlsInitTestPeer::hello_rand(tls_init));
    const auto first_chunk = response.size() / 2;
    ASSERT_TRUE(flush_response_into_tls_init(tls_init, socket_pair.peer, td::Slice(response).substr(0, first_chunk),
                                             first_chunk)
                    .is_ok());
    auto status = TlsInitTestPeer::wait_hello_response(tls_init);
    if (status.is_ok()) {
      ASSERT_TRUE(
          flush_response_into_tls_init(tls_init, socket_pair.peer, td::Slice(response).substr(first_chunk)).is_ok());
      status = TlsInitTestPeer::wait_hello_response(tls_init);
    }
    ASSERT_TRUE(status.is_error());
    ASSERT_EQ(test_case.expected_code, status.code());

    auto classification = td::classify_connection_failure(true, tls_proxy(), status);
    ASSERT_TRUE(classification.proxy_backed);
    ASSERT_TRUE(classification.is_deterministic_proxy_rejection());
    ASSERT_TRUE(classification.apply_exponential_backoff);
    ASSERT_TRUE(classification.bounded_retry);
    ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::TlsHello), static_cast<td::int32>(classification.stage));
    ASSERT_EQ(static_cast<td::int32>(test_case.expected_reason), static_cast<td::int32>(classification.reason));

    assert_fail_closed_backoff();
  }
}

}  // namespace

#endif  // TD_PORT_POSIX
