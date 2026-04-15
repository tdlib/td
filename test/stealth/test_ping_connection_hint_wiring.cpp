// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/PingConnection.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/stealth/Interfaces.h"

#include "td/utils/BufferedFd.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::AuthData;
using td::mtproto::AuthKey;
using td::mtproto::PingConnection;
using td::mtproto::RawConnection;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::create_socket_pair;
using td::mtproto::TransportType;

class NoopStatsCallback final : public RawConnection::StatsCallback {
 public:
  void on_read(td::uint64 bytes) final {
  }
  void on_write(td::uint64 bytes) final {
  }
  void on_pong() final {
  }
  void on_error() final {
  }
  void on_mtproto_error() final {
  }
};

class HintCapturingRawConnection final : public RawConnection {
 public:
  explicit HintCapturingRawConnection(td::BufferedFd<td::SocketFd> fd) : fd_(std::move(fd)) {
  }

  void set_connection_token(td::mtproto::ConnectionManager::ConnectionToken connection_token) final {
  }

  bool can_send() const final {
    return true;
  }

  TransportType get_transport_type() const final {
    return TransportType{TransportType::Tcp, 0, td::mtproto::ProxySecret()};
  }

  size_t send_crypto(const td::Storer &storer, td::uint64 session_id, td::int64 salt, const AuthKey &auth_key,
                     td::uint64 quick_ack_token, TrafficHint hint) final {
    (void)storer;
    (void)session_id;
    (void)salt;
    (void)auth_key;
    (void)quick_ack_token;
    sent_hints.push_back(hint);
    return 0;
  }

  void send_no_crypto(const td::Storer &storer, TrafficHint hint) final {
    (void)storer;
    sent_hints.push_back(hint);
  }

  td::PollableFdInfo &get_poll_info() final {
    return fd_.get_poll_info();
  }

  StatsCallback *stats_callback() final {
    return &stats_callback_;
  }

  double shaping_wakeup_at() const final {
    return 0.0;
  }

  td::Status flush(const td::mtproto::AuthKey &auth_key, Callback &callback) final {
    (void)auth_key;
    flush_calls_++;
    return callback.before_write();
  }

  bool has_error() const final {
    return false;
  }

  void close() final {
  }

  PublicFields &extra() final {
    return extra_;
  }

  const PublicFields &extra() const final {
    return extra_;
  }

  td::vector<TrafficHint> sent_hints;
  int flush_calls_{0};

 private:
  td::BufferedFd<td::SocketFd> fd_;
  PublicFields extra_;
  NoopStatsCallback stats_callback_;
};

td::unique_ptr<AuthData> make_ready_auth_data() {
  auto auth_data = td::make_unique<AuthData>();
  auth_data->set_session_mode(false);
  auth_data->set_main_auth_key(AuthKey(1, td::string(256, 'a')));
  auth_data->set_server_salt(1, td::Time::now_cached());
  auth_data->set_future_salts({td::mtproto::ServerSalt{2, -1e9, 1e9}}, td::Time::now_cached());
  auth_data->set_session_id(1);
  return auth_data;
}

TEST(PingConnectionHintWiring, PingPongAuthenticatedPathUsesKeepaliveHint) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<HintCapturingRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  auto *raw_ptr = raw_connection.get();

  auto ping_connection = PingConnection::create_ping_pong(std::move(raw_connection), make_ready_auth_data());
  ASSERT_TRUE(ping_connection->flush().is_ok());

  ASSERT_EQ(1, raw_ptr->flush_calls_);
  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, raw_ptr->sent_hints[0]);
}

}  // namespace