// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/Handshake.h"
#include "td/mtproto/HandshakeConnection.h"
#include "td/mtproto/PingConnection.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/RSA.h"
#include "td/mtproto/stealth/Interfaces.h"

#include "td/utils/BufferedFd.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::AuthKeyHandshake;
using td::mtproto::AuthKeyHandshakeContext;
using td::mtproto::DhCallback;
using td::mtproto::HandshakeConnection;
using td::mtproto::PingConnection;
using td::mtproto::PublicRsaKeyInterface;
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

  size_t send_crypto(const td::Storer &storer, td::uint64 session_id, td::int64 salt,
                     const td::mtproto::AuthKey &auth_key, td::uint64 quick_ack_token, TrafficHint hint) final {
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
    (void)callback;
    flush_calls_++;
    return td::Status::OK();
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

class NullHandshakeContext final : public AuthKeyHandshakeContext {
 public:
  DhCallback *get_dh_callback() final {
    return nullptr;
  }

  PublicRsaKeyInterface *get_public_rsa_key_interface() final {
    return nullptr;
  }
};

TEST(HandshakeBootstrapHints, HandshakeConnectionResumeUsesAuthHandshakeHint) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<HintCapturingRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  auto *raw_ptr = raw_connection.get();

  AuthKeyHandshake handshake(2, 0);
  HandshakeConnection connection(std::move(raw_connection), &handshake, td::make_unique<NullHandshakeContext>());

  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::AuthHandshake, raw_ptr->sent_hints[0]);
}

TEST(HandshakeBootstrapHints, ReqPqPingUsesAuthHandshakeHint) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<HintCapturingRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  auto *raw_ptr = raw_connection.get();

  auto ping_connection = PingConnection::create_req_pq(std::move(raw_connection), 1);
  ASSERT_TRUE(ping_connection->flush().is_ok());

  ASSERT_EQ(1, raw_ptr->flush_calls_);
  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::AuthHandshake, raw_ptr->sent_hints[0]);
}

}  // namespace