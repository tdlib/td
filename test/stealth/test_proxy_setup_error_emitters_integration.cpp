// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/net/HttpProxy.h"
#include "td/net/ProxySetupError.h"
#include "td/net/Socks5.h"

#include "td/actor/ConcurrentScheduler.h"

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/utils/base64.h"
#include "td/utils/common.h"
#include "td/utils/port/config.h"
#include "td/utils/tests.h"

#if TD_PORT_POSIX

namespace {

using td::mtproto::test::create_socket_pair;
using td::mtproto::test::read_exact;
using td::mtproto::test::write_all;

struct ProxyActorObservation final {
  bool finished{false};
  bool success{false};
  td::Status error;
  td::int32 connected_calls{0};
};

class RecordingCallback final : public td::TransparentProxy::Callback {
 public:
  explicit RecordingCallback(ProxyActorObservation *observation) : observation_(observation) {
  }

  void set_result(td::Result<td::BufferedFd<td::SocketFd>> result) final {
    observation_->finished = true;
    observation_->success = result.is_ok();
    if (result.is_error()) {
      observation_->error = result.move_as_error();
    }
    td::Scheduler::instance()->finish();
  }

  void on_connected() final {
    observation_->connected_calls++;
  }

 private:
  ProxyActorObservation *observation_;
};

template <class CreateActorT, class InteractT>
ProxyActorObservation run_proxy_actor(CreateActorT &&create_actor, InteractT &&interact) {
  auto socket_pair = create_socket_pair().move_as_ok();
  ProxyActorObservation observation;
  td::ConcurrentScheduler scheduler(0, 0);

  {
    auto guard = scheduler.get_main_guard();
    create_actor(std::move(socket_pair.client), &observation);
  }

  scheduler.start();
  interact(scheduler, socket_pair.peer, observation);

  td::int32 iterations = 0;
  while (scheduler.run_main(10)) {
    iterations++;
    ASSERT_TRUE(iterations < 1000);
  }
  scheduler.finish();
  return observation;
}

td::IPAddress test_mtproto_ip() {
  td::IPAddress ip_address;
  ip_address.init_ipv4_port("149.154.167.50", 443).ensure();
  return ip_address;
}

td::string expected_http_connect_request(const td::string &host, td::int32 port, td::Slice user, td::Slice password) {
  td::string host_port = PSTRING() << host << ':' << port;
  td::string proxy_authorization;
  if (!user.empty() || !password.empty()) {
    proxy_authorization = PSTRING() << "Proxy-Authorization: Basic "
                                    << td::base64_encode(PSTRING() << user << ':' << password) << "\r\n";
  }
  return PSTRING() << "CONNECT " << host_port << " HTTP/1.1\r\n"
                   << "Host: " << host_port << "\r\n"
                   << proxy_authorization << "\r\n";
}

TEST(ProxySetupErrorEmittersIntegration, HttpProxyEmitsConnectRequestWithoutCredentials) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::HttpProxy>("HttpProxy", std::move(client), test_mtproto_ip(), "", "",
                                        td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &) {
        scheduler.run_main(10);
        auto expected = expected_http_connect_request("149.154.167.50", 443, "", "");
        ASSERT_EQ(expected, read_exact(peer, expected.size()).move_as_ok());
        ASSERT_TRUE(write_all(peer, "HTTP/1.1 200 OK\r\n\r\n").is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_TRUE(observation.success);
  ASSERT_EQ(0, observation.connected_calls);
}

TEST(ProxySetupErrorEmittersIntegration, HttpProxyEmitsBasicAuthorizationHeaderWhenCredentialsPresent) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::HttpProxy>("HttpProxy", std::move(client), test_mtproto_ip(), "alice", "s3cret",
                                        td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &) {
        scheduler.run_main(10);
        auto expected = expected_http_connect_request("149.154.167.50", 443, "alice", "s3cret");
        ASSERT_EQ(expected, read_exact(peer, expected.size()).move_as_ok());
        ASSERT_TRUE(write_all(peer, "HTTP/1.0 200 Connection established\r\n\r\n").is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_TRUE(observation.success);
}

TEST(ProxySetupErrorEmittersIntegration, HttpProxyRejectsNon2xxResponseWithTypedError) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::HttpProxy>("HttpProxy", std::move(client), test_mtproto_ip(), "", "",
                                        td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &) {
        scheduler.run_main(10);
        auto expected = expected_http_connect_request("149.154.167.50", 443, "", "");
        ASSERT_EQ(expected, read_exact(peer, expected.size()).move_as_ok());
        ASSERT_TRUE(write_all(peer, "HTTP/1.1 403 Forbidden\r\nX-Test: blocked\r\n\r\n").is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_FALSE(observation.success);
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::HttpConnectRejected), observation.error.code());
}

TEST(ProxySetupErrorEmittersIntegration, HttpProxyRejectsMalformedStatusLineWithBinaryTail) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::HttpProxy>("HttpProxy", std::move(client), test_mtproto_ip(), "", "",
                                        td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &) {
        scheduler.run_main(10);
        auto expected = expected_http_connect_request("149.154.167.50", 443, "", "");
        ASSERT_EQ(expected, read_exact(peer, expected.size()).move_as_ok());

        td::string malformed = "HTTP/1.1 xxx";
        malformed.push_back('\n');
        malformed.push_back('\x01');
        malformed.push_back('\x02');
        malformed += "\r\n\r\n";
        ASSERT_TRUE(write_all(peer, malformed).is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_FALSE(observation.success);
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::HttpConnectRejected), observation.error.code());
}

TEST(ProxySetupErrorEmittersIntegration, HttpProxyWaitsForHeaderTerminatorBeforeSucceeding) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::HttpProxy>("HttpProxy", std::move(client), test_mtproto_ip(), "", "",
                                        td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &observation) {
        scheduler.run_main(10);
        auto expected = expected_http_connect_request("149.154.167.50", 443, "", "");
        ASSERT_EQ(expected, read_exact(peer, expected.size()).move_as_ok());

        ASSERT_TRUE(write_all(peer, "HTTP/1.1 200 OK\r\nProxy-Agent: test").is_ok());
        scheduler.run_main(10);
        ASSERT_FALSE(observation.finished);

        ASSERT_TRUE(write_all(peer, "\r\n\r\n").is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_TRUE(observation.success);
}

TEST(ProxySetupErrorEmittersIntegration, Socks5GreetingWithoutCredentialsAdvertisesOnlyNoAuth) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::Socks5>("Socks5", std::move(client), test_mtproto_ip(), "", "",
                                     td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &) {
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x05\x01\x00", 3), read_exact(peer, 3).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x00", 2)).is_ok());
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x05\x01\x00\x01\x95\x9a\xa7\x32\x01\xbb", 10), read_exact(peer, 10).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10)).is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_TRUE(observation.success);
  ASSERT_EQ(1, observation.connected_calls);
}

TEST(ProxySetupErrorEmittersIntegration, Socks5UsernamePasswordFlowSendsCredentialsAndConnectRequest) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::Socks5>("Socks5", std::move(client), test_mtproto_ip(), "alice", "s3cret",
                                     td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &observation) {
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x05\x02\x00\x02", 4), read_exact(peer, 4).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x02", 2)).is_ok());

        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x01\x05"
                             "alice"
                             "\x06"
                             "s3cret",
                             14),
                  read_exact(peer, 14).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x01\x00", 2)).is_ok());

        scheduler.run_main(10);
        ASSERT_EQ(1, observation.connected_calls);
        ASSERT_EQ(td::string("\x05\x01\x00\x01\x95\x9a\xa7\x32\x01\xbb", 10), read_exact(peer, 10).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10)).is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_TRUE(observation.success);
  ASSERT_EQ(1, observation.connected_calls);
}

TEST(ProxySetupErrorEmittersIntegration, Socks5RejectsUnsupportedGreetingVersionWithTypedError) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::Socks5>("Socks5", std::move(client), test_mtproto_ip(), "", "",
                                     td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &) {
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x05\x01\x00", 3), read_exact(peer, 3).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x48\x00", 2)).is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_FALSE(observation.success);
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::SocksUnsupportedVersion), observation.error.code());
  ASSERT_EQ(0, observation.connected_calls);
}

TEST(ProxySetupErrorEmittersIntegration, Socks5RejectsUnsupportedAuthenticationModeWithTypedError) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::Socks5>("Socks5", std::move(client), test_mtproto_ip(), "", "",
                                     td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &) {
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x05\x01\x00", 3), read_exact(peer, 3).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x7f", 2)).is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_FALSE(observation.success);
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::SocksUnsupportedAuthenticationMode),
            observation.error.code());
  ASSERT_EQ(0, observation.connected_calls);
}

TEST(ProxySetupErrorEmittersIntegration, Socks5RejectsUnsupportedSubnegotiationVersionWithTypedError) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::Socks5>("Socks5", std::move(client), test_mtproto_ip(), "alice", "s3cret",
                                     td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &) {
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x05\x02\x00\x02", 4), read_exact(peer, 4).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x02", 2)).is_ok());
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x01\x05"
                             "alice"
                             "\x06"
                             "s3cret",
                             14),
                  read_exact(peer, 14).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x02\x00", 2)).is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_FALSE(observation.success);
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::SocksUnsupportedSubnegotiationVersion),
            observation.error.code());
  ASSERT_EQ(0, observation.connected_calls);
}

TEST(ProxySetupErrorEmittersIntegration, Socks5RejectsWrongUsernameOrPasswordWithTypedError) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::Socks5>("Socks5", std::move(client), test_mtproto_ip(), "alice", "s3cret",
                                     td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &) {
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x05\x02\x00\x02", 4), read_exact(peer, 4).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x02", 2)).is_ok());
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x01\x05"
                             "alice"
                             "\x06"
                             "s3cret",
                             14),
                  read_exact(peer, 14).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x01\x01", 2)).is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_FALSE(observation.success);
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::SocksWrongUsernameOrPassword), observation.error.code());
  ASSERT_EQ(0, observation.connected_calls);
}

TEST(ProxySetupErrorEmittersIntegration, Socks5ConnectRejectReturnsTypedErrorAfterConnectCallback) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::Socks5>("Socks5", std::move(client), test_mtproto_ip(), "", "",
                                     td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &observation) {
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x05\x01\x00", 3), read_exact(peer, 3).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x00", 2)).is_ok());
        scheduler.run_main(10);
        ASSERT_EQ(1, observation.connected_calls);
        ASSERT_EQ(td::string("\x05\x01\x00\x01\x95\x9a\xa7\x32\x01\xbb", 10), read_exact(peer, 10).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x05\x00\x01\x00\x00\x00\x00\x00\x00", 10)).is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_FALSE(observation.success);
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::SocksConnectRejected), observation.error.code());
  ASSERT_EQ(1, observation.connected_calls);
}

TEST(ProxySetupErrorEmittersIntegration, Socks5InvalidResponseReturnsTypedErrorAfterConnectCallback) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::Socks5>("Socks5", std::move(client), test_mtproto_ip(), "", "",
                                     td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &observation) {
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x05\x01\x00", 3), read_exact(peer, 3).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x00", 2)).is_ok());
        scheduler.run_main(10);
        ASSERT_EQ(1, observation.connected_calls);
        ASSERT_EQ(td::string("\x05\x01\x00\x01\x95\x9a\xa7\x32\x01\xbb", 10), read_exact(peer, 10).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x00\x01\x03", 4)).is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_FALSE(observation.success);
  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::SocksInvalidResponse), observation.error.code());
  ASSERT_EQ(1, observation.connected_calls);
}

TEST(ProxySetupErrorEmittersIntegration, Socks5RejectsOverlongUsernameLocally) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::Socks5>("Socks5", std::move(client), test_mtproto_ip(), td::string(128, 'a'), "pwd",
                                     td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &) {
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x05\x02\x00\x02", 4), read_exact(peer, 4).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x02", 2)).is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_FALSE(observation.success);
  ASSERT_EQ("Username is too long", observation.error.message());
}

TEST(ProxySetupErrorEmittersIntegration, Socks5RejectsOverlongPasswordLocally) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto observation = run_proxy_actor(
      [](td::SocketFd client, ProxyActorObservation *observation) {
        td::create_actor<td::Socks5>("Socks5", std::move(client), test_mtproto_ip(), "user", td::string(128, 'p'),
                                     td::make_unique<RecordingCallback>(observation), td::ActorShared<>())
            .release();
      },
      [](td::ConcurrentScheduler &scheduler, td::SocketFd &peer, ProxyActorObservation &) {
        scheduler.run_main(10);
        ASSERT_EQ(td::string("\x05\x02\x00\x02", 4), read_exact(peer, 4).move_as_ok());
        ASSERT_TRUE(write_all(peer, td::string("\x05\x02", 2)).is_ok());
      });

  ASSERT_TRUE(observation.finished);
  ASSERT_FALSE(observation.success);
  ASSERT_EQ("Password is too long", observation.error.message());
}

}  // namespace

#endif  // TD_PORT_POSIX