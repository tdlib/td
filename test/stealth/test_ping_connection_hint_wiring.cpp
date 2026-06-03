// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/mtproto_api.h"
#include "td/mtproto/PingConnection.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/utils.h"

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

class ReqPqErrorRawConnection final : public RawConnection {
 public:
  explicit ReqPqErrorRawConnection(td::BufferedFd<td::SocketFd> fd) : fd_(std::move(fd)) {
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
    (void)hint;
    return 0;
  }

  void send_no_crypto(const td::Storer &storer, TrafficHint hint) final {
    (void)storer;
    (void)hint;
    sent_no_crypto_calls_++;
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
    if (auto before_write = callback.before_write(); before_write.is_error()) {
      return before_write;
    }
    td::mtproto::PacketInfo packet_info;
    return callback.on_raw_packet(packet_info, td::BufferSlice("short"));
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

  int flush_calls_{0};
  int sent_no_crypto_calls_{0};

 private:
  td::BufferedFd<td::SocketFd> fd_;
  PublicFields extra_;
  NoopStatsCallback stats_callback_;
};

td::UInt128 make_fixed_nonce() {
  td::UInt128 nonce;
  for (size_t i = 0; i < sizeof(nonce.raw); i++) {
    nonce.raw[i] = static_cast<unsigned char>(0xAB);
  }
  return nonce;
}

td::string build_res_pq_payload(const td::UInt128 &response_nonce) {
  td::UInt128 server_nonce;
  for (size_t i = 0; i < sizeof(server_nonce.raw); i++) {
    server_nonce.raw[i] = static_cast<unsigned char>(0x20 + i);
  }

  const td::vector<td::int64> fingerprints = {0x1111111111111111LL, 0x2222222222222222LL};
  const td::string pq("\x13\x37", 2);
  td::mtproto_api::array<td::int64> server_public_key_fingerprints(fingerprints.begin(), fingerprints.end());
  td::mtproto_api::resPQ res_pq(response_nonce, server_nonce, pq, std::move(server_public_key_fingerprints));
  td::TLObjectStorer<td::mtproto_api::resPQ> storer(res_pq);

  td::string payload(storer.size(), '\0');
  auto real_size = storer.store(td::MutableSlice(payload).ubegin());
  payload.resize(real_size);
  return payload;
}

class ReqPqScriptedRawConnection final : public RawConnection {
 public:
  enum class Mode { ShortPayload, MalformedPayload, NonceMismatchPayload };

  ReqPqScriptedRawConnection(td::BufferedFd<td::SocketFd> fd, Mode mode) : fd_(std::move(fd)), mode_(mode) {
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
    (void)hint;
    return 0;
  }

  void send_no_crypto(const td::Storer &storer, TrafficHint hint) final {
    (void)storer;
    (void)hint;
    sent_no_crypto_calls_++;
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
    if (auto before_write = callback.before_write(); before_write.is_error()) {
      return before_write;
    }

    td::string payload;
    switch (mode_) {
      case Mode::ShortPayload:
        payload = "short";
        break;
      case Mode::MalformedPayload:
        payload = td::string(12, 'x');
        break;
      case Mode::NonceMismatchPayload:
        payload = build_res_pq_payload(make_fixed_nonce());
        break;
    }

    td::mtproto::PacketInfo packet_info;
    return callback.on_raw_packet(packet_info, td::BufferSlice(payload));
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

  int flush_calls_{0};
  int sent_no_crypto_calls_{0};

 private:
  td::BufferedFd<td::SocketFd> fd_;
  Mode mode_;
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

TEST(PingConnectionHintWiring, ReqPqShortResponseReturnsContextualParseError) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<ReqPqErrorRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  auto *raw_ptr = raw_connection.get();

  auto ping_connection = PingConnection::create_req_pq(std::move(raw_connection), 1);
  auto status = ping_connection->flush();

  ASSERT_TRUE(status.is_error());
  ASSERT_EQ(1, raw_ptr->flush_calls_);
  ASSERT_EQ(1, raw_ptr->sent_no_crypto_calls_);
  auto message = status.message().str();
  ASSERT_TRUE(message.find("req_pq response packet is too small") != td::string::npos);
  ASSERT_TRUE(message.find("packet_bytes=5") != td::string::npos);
  ASSERT_TRUE(message.find("min_bytes=12") != td::string::npos);
}

TEST(PingConnectionHintWiring, ReqPqRejectsZeroPingCountFailClosed) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<ReqPqErrorRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  auto *raw_ptr = raw_connection.get();

  auto ping_connection = PingConnection::create_req_pq(std::move(raw_connection), 0);
  auto status = ping_connection->flush();

  ASSERT_TRUE(status.is_error());
  auto message = status.message().str();
  ASSERT_TRUE(message.find("req_pq ping_count must be positive") != td::string::npos);
  ASSERT_TRUE(message.find("ping_count=0") != td::string::npos);
  ASSERT_EQ(0, raw_ptr->flush_calls_);
  ASSERT_EQ(0, raw_ptr->sent_no_crypto_calls_);
}

TEST(PingConnectionHintWiring, ReqPqMalformedMinimumPayloadFailsClosed) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection = td::make_unique<ReqPqScriptedRawConnection>(
      td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)), ReqPqScriptedRawConnection::Mode::MalformedPayload);
  auto *raw_ptr = raw_connection.get();

  auto ping_connection = PingConnection::create_req_pq(std::move(raw_connection), 1);
  auto status = ping_connection->flush();

  ASSERT_TRUE(status.is_error());
  auto message = status.message().str();
  ASSERT_TRUE(message.find("failed to parse req_pq response payload") != td::string::npos);
  ASSERT_TRUE(message.find("packet_bytes=12") != td::string::npos);
  ASSERT_EQ(1, raw_ptr->flush_calls_);
  ASSERT_EQ(1, raw_ptr->sent_no_crypto_calls_);
}

TEST(PingConnectionHintWiring, ReqPqNonceMismatchFailsClosed) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<ReqPqScriptedRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)),
                                                  ReqPqScriptedRawConnection::Mode::NonceMismatchPayload);
  auto *raw_ptr = raw_connection.get();

  auto ping_connection = PingConnection::create_req_pq(std::move(raw_connection), 1);
  auto status = ping_connection->flush();

  ASSERT_TRUE(status.is_error());
  auto message = status.message().str();
  ASSERT_TRUE(message.find("req_pq response nonce mismatch") != td::string::npos);
  ASSERT_TRUE(message.find("packet_bytes=") != td::string::npos);
  ASSERT_EQ(1, raw_ptr->flush_calls_);
  ASSERT_EQ(1, raw_ptr->sent_no_crypto_calls_);
}

}  // namespace