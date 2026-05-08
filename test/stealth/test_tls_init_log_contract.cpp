// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Time.h"

#include "td/mtproto/TlsInit.h"

#include "test/stealth/TlsInitTestHelpers.h"
#include "test/stealth/TlsInitTestPeer.h"

#include "td/utils/tests.h"

#include "td/utils/port/config.h"

#if defined(TD_PORT_POSIX) && TD_PORT_POSIX

namespace {

using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::make_tls_init_response;
using td::mtproto::test::TlsInitTestPeer;
using td::mtproto::test::write_all;
using td::mtproto::TlsInit;

constexpr td::Slice kSecret("0123456789secret");
constexpr td::Slice kFirstResponsePrefix("\x16\x03\x03");
constexpr td::Slice kSecondResponsePrefix("\x14\x03\x03\x00\x01\x01\x17\x03\x03");
constexpr td::int32 kCircuitBreakerUnixTime = 20000 * 86400 + 1800;

class NoopCallback final : public td::TransparentProxy::Callback {
 public:
  void set_result(td::Result<td::BufferedFd<td::SocketFd>>) final {
  }

  void on_connected() final {
  }
};

class CapturingLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    entries.push_back(slice.str());
  }

  bool contains(td::Slice needle) const {
    auto needle_str = needle.str();
    for (const auto &entry : entries) {
      if (entry.find(needle_str) != td::string::npos) {
        return true;
      }
    }
    return false;
  }

  td::string joined() const {
    td::string result;
    for (const auto &entry : entries) {
      result += entry;
      result += '\n';
    }
    return result;
  }

 private:
  td::vector<td::string> entries;
};

TlsInit create_tls_init(td::SocketFd socket_fd, const NetworkRouteHints &route_hints, td::int32 unix_time) {
  auto diff = static_cast<double>(unix_time) - td::Time::now();
  return TlsInit(std::move(socket_fd), "www.google.com", kSecret.str(), td::make_unique<NoopCallback>(), {}, diff,
                 route_hints);
}

td::Status feed_invalid_hash_response(TlsInit &tls_init, td::SocketFd &peer_fd) {
  auto response = make_tls_init_response(kSecret, TlsInitTestPeer::hello_rand(tls_init), kFirstResponsePrefix,
                                         kSecondResponsePrefix);
  response[11] ^= 0x01;
  TRY_STATUS(write_all(peer_fd, response));
  TlsInitTestPeer::fd(tls_init).get_poll_info().add_flags(td::PollFlags::Read());
  TRY_RESULT(read_size, TlsInitTestPeer::fd(tls_init).flush_read());
  (void)read_size;
  return TlsInitTestPeer::wait_hello_response(tls_init);
}

TEST(TlsInitLogContract, PreparedHelloLogIncludesProfileAndRouteDecisionFields) {
  SKIP_IF_NO_SOCKET_PAIR();
  reset_runtime_ech_failure_state_for_tests();

  NetworkRouteHints ru_route;
  ru_route.is_known = true;
  ru_route.is_ru = true;

  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client), ru_route, kCircuitBreakerUnixTime);

  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
  };

  TlsInitTestPeer::send_hello(tls_init);

  auto captured = capture.joined();
  td::store_active_log_interface(old_sink);
  SET_VERBOSITY_LEVEL(old_verbosity);

  ASSERT_TRUE(captured.find("TlsInit hello prepared") != td::string::npos);
  ASSERT_TRUE(captured.find("[profile:") != td::string::npos);
  ASSERT_TRUE(captured.find("[ech_mode:disabled]") != td::string::npos);
  ASSERT_TRUE(captured.find("[ech_disabled_by_route:true]") != td::string::npos);
  ASSERT_TRUE(captured.find("[ech_disabled_by_circuit_breaker:false]") != td::string::npos);
  ASSERT_TRUE(captured.find("[ech_reenabled_after_ttl:false]") != td::string::npos);
}

TEST(TlsInitLogContract, RejectionLogIncludesProfileAndCircuitBreakerDecisionFields) {
  SKIP_IF_NO_SOCKET_PAIR();
  reset_runtime_ech_failure_state_for_tests();

  NetworkRouteHints non_ru_route;
  non_ru_route.is_known = true;
  non_ru_route.is_ru = false;

  for (int i = 0; i < 3; i++) {
    auto socket_pair = create_socket_pair().move_as_ok();
    auto tls_init = create_tls_init(std::move(socket_pair.client), non_ru_route, kCircuitBreakerUnixTime);
    TlsInitTestPeer::send_hello(tls_init);

    auto status = feed_invalid_hash_response(tls_init, socket_pair.peer);
    ASSERT_TRUE(status.is_error());
  }

  auto socket_pair = create_socket_pair().move_as_ok();
  auto tls_init = create_tls_init(std::move(socket_pair.client), non_ru_route, kCircuitBreakerUnixTime);

  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
  };

  TlsInitTestPeer::send_hello(tls_init);
  auto status = feed_invalid_hash_response(tls_init, socket_pair.peer);
  auto captured = capture.joined();
  td::store_active_log_interface(old_sink);
  SET_VERBOSITY_LEVEL(old_verbosity);
  ASSERT_TRUE(status.is_error());

  ASSERT_TRUE(captured.find("TlsInit hello response rejected") != td::string::npos);
  ASSERT_TRUE(captured.find("[profile:") != td::string::npos);
  ASSERT_TRUE(captured.find("[ech_mode:disabled]") != td::string::npos);
  ASSERT_TRUE(captured.find("[ech_disabled_by_route:false]") != td::string::npos);
  ASSERT_TRUE(captured.find("[ech_disabled_by_circuit_breaker:true]") != td::string::npos);
  ASSERT_TRUE(captured.find("[ech_reenabled_after_ttl:false]") != td::string::npos);
  ASSERT_TRUE(captured.find("[failure_stage:response_hash]") != td::string::npos);
  ASSERT_TRUE(captured.find("[recorded_ech_failure:false]") != td::string::npos);
}

}  // namespace

#endif  // defined(TD_PORT_POSIX) && TD_PORT_POSIX