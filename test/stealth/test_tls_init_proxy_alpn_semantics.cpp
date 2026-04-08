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
#include "td/utils/Time.h"

#define private public
#define protected public
#include "td/mtproto/TlsInit.h"
#undef protected
#undef private

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/tests.h"

#if !TD_DARWIN

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::read_exact;
using td::mtproto::TlsInit;

class NoopCallback final : public td::TransparentProxy::Callback {
 public:
  void set_result(td::Result<td::BufferedFd<td::SocketFd>>) final {
  }

  void on_connected() final {
  }
};

td::string flush_client_hello(TlsInit &tls_init, td::SocketFd &peer_fd) {
  auto bytes_to_read = tls_init.fd_.ready_for_flush_write();
  CHECK(bytes_to_read > 0);
  tls_init.fd_.get_poll_info().add_flags(td::PollFlags::Write());
  while (tls_init.fd_.ready_for_flush_write() > 0) {
    auto flush_status = tls_init.fd_.flush_write();
    CHECK(flush_status.is_ok());
  }
  return read_exact(peer_fd, bytes_to_read).move_as_ok();
}

td::Slice browser_alpn_body() {
  static const td::string value("\x00\x0c\x02\x68\x32\x08\x68\x74\x74\x70\x2f\x31\x2e\x31", 14);
  return value;
}

td::Slice http11_only_alpn_body() {
  static const td::string value("\x00\x09\x08\x68\x74\x74\x70\x2f\x31\x2e\x31", 11);
  return value;
}

TEST(TlsInitProxyAlpnSemantics, ExplicitProfileBuilderKeepsBrowserCaptureAlpnBody) {
  MockRng rng(1);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  auto *alpn = find_extension(parsed.ok(), 0x0010);
  ASSERT_TRUE(alpn != nullptr);
  ASSERT_EQ(browser_alpn_body(), alpn->value);
}

TEST(TlsInitProxyAlpnSemantics, KnownNonRuProxyHelloAdvertisesHttp11Only) {
  reset_runtime_ech_failure_state_for_tests();
  auto socket_pair = create_socket_pair().move_as_ok();

  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;

  constexpr td::int32 kUnixTime = 1712345678;
  auto server_time_difference = static_cast<double>(kUnixTime) - td::Time::now();
  TlsInit tls_init(std::move(socket_pair.client), "www.google.com", "0123456789secret", td::make_unique<NoopCallback>(),
                   {}, server_time_difference, route_hints);
  tls_init.send_hello();

  auto wire = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  auto *alpn = find_extension(parsed.ok(), 0x0010);
  ASSERT_TRUE(alpn != nullptr);
  ASSERT_EQ(http11_only_alpn_body(), alpn->value);
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);
}

TEST(TlsInitProxyAlpnSemantics, UnknownRouteKeepsHttp11OnlyAndSuppressesEch) {
  reset_runtime_ech_failure_state_for_tests();
  auto socket_pair = create_socket_pair().move_as_ok();

  NetworkRouteHints route_hints;
  route_hints.is_known = false;
  route_hints.is_ru = false;

  constexpr td::int32 kUnixTime = 1712345678;
  auto server_time_difference = static_cast<double>(kUnixTime) - td::Time::now();
  TlsInit tls_init(std::move(socket_pair.client), "www.google.com", "0123456789secret", td::make_unique<NoopCallback>(),
                   {}, server_time_difference, route_hints);
  tls_init.send_hello();

  auto wire = flush_client_hello(tls_init, socket_pair.peer);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  auto *alpn = find_extension(parsed.ok(), 0x0010);
  ASSERT_TRUE(alpn != nullptr);
  ASSERT_EQ(http11_only_alpn_body(), alpn->value);
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
}

}  // namespace

#endif
