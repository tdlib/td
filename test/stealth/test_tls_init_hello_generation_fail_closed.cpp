// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/actor/ConcurrentScheduler.h"

#include "td/mtproto/TlsInit.h"

#include "td/net/ProxySetupError.h"

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/utils/common.h"
#include "td/utils/port/config.h"
#include "td/utils/tests.h"

#if TD_PORT_POSIX

namespace {

using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::create_socket_pair;
using td::mtproto::TlsInit;

struct HelloGenerationObservation final {
  bool finished{false};
  bool success{false};
  td::Status error;
  td::int32 connected_calls{0};
};

class RecordingCallback final : public td::TransparentProxy::Callback {
 public:
  explicit RecordingCallback(HelloGenerationObservation *observation) : observation_(observation) {
  }

  void set_result(td::Result<td::BufferedFd<td::SocketFd>> result) final {
    observation_->finished = true;
    observation_->success = result.is_ok();
    if (result.is_error()) {
      observation_->error = result.move_as_error();
    }
  }

  void on_connected() final {
    observation_->connected_calls++;
  }

 private:
  HelloGenerationObservation *observation_;
};

HelloGenerationObservation run_tls_init_hello_generation(td::string domain, td::string secret) {
  auto socket_pair = create_socket_pair().move_as_ok();
  HelloGenerationObservation observation;
  td::ConcurrentScheduler scheduler(0, 0);

  {
    auto guard = scheduler.get_main_guard();
    NetworkRouteHints route_hints;
    route_hints.is_known = true;
    route_hints.is_ru = false;
    td::create_actor<TlsInit>("TlsInitHelloGenerationFailClosed", std::move(socket_pair.client), std::move(domain),
                              std::move(secret), td::make_unique<RecordingCallback>(&observation), td::ActorShared<>(),
                              0.0, route_hints)
        .release();
  }

  scheduler.start();
  td::int32 iterations = 0;
  while (!observation.finished && scheduler.run_main(10)) {
    iterations++;
    ASSERT_TRUE(iterations < 1000);
  }
  scheduler.finish();
  ASSERT_TRUE(observation.finished);
  return observation;
}

TEST(TlsInitHelloGenerationFailClosed, EmptyDomainReturnsTypedProxySetupError) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_tls_init_hello_generation("", "0123456789secret");
  ASSERT_FALSE(observation.success);
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse), observation.error.code());
  ASSERT_TRUE(observation.error.message().str().find("domain") != td::string::npos);
  ASSERT_EQ(0, observation.connected_calls);
}

TEST(TlsInitHelloGenerationFailClosed, ShortSecretReturnsTypedProxySetupError) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_tls_init_hello_generation("proxy.example.com", "short-secret");
  ASSERT_FALSE(observation.success);
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse), observation.error.code());
  ASSERT_TRUE(observation.error.message().str().find("16 bytes") != td::string::npos);
  ASSERT_EQ(0, observation.connected_calls);
}

}  // namespace

#endif
