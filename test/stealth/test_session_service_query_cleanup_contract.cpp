// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/SessionConnectionTestPeer.h"
#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/SessionConnection.h"

#include "td/utils/BufferedFd.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::AuthData;
using td::mtproto::AuthKey;
using td::mtproto::RawConnection;
using td::mtproto::SessionConnection;
using td::mtproto::TransportType;
using td::mtproto::test::SessionConnectionTestPeer;
using td::mtproto::test::create_socket_pair;
using td::mtproto::stealth::TrafficHint;

class NoopStatsCallback final : public RawConnection::StatsCallback {
 public:
  void on_read(td::uint64) final {
  }
  void on_write(td::uint64) final {
  }
  void on_pong() final {
  }
  void on_error() final {
  }
  void on_mtproto_error() final {
  }
};

class SendOnlyRawConnection final : public RawConnection {
 public:
  explicit SendOnlyRawConnection(td::BufferedFd<td::SocketFd> fd) : fd_(std::move(fd)) {
  }

  void set_connection_token(td::mtproto::ConnectionManager::ConnectionToken connection_token) final {
  }

  bool can_send() const final {
    return true;
  }

  TransportType get_transport_type() const final {
    return TransportType{TransportType::Tcp, 0, td::mtproto::ProxySecret()};
  }

  size_t send_crypto(const td::Storer &, td::uint64, td::int64, const AuthKey &, td::uint64, TrafficHint) final {
    send_crypto_calls_++;
    return 0;
  }

  void send_no_crypto(const td::Storer &, TrafficHint) final {
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

  td::Status flush(const AuthKey &, Callback &callback) final {
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

  int flush_calls_{0};
  int send_crypto_calls_{0};

 private:
  td::BufferedFd<td::SocketFd> fd_;
  PublicFields extra_;
  NoopStatsCallback stats_callback_;
};

class NoopSessionCallback : public SessionConnection::Callback {
 public:
  void on_connected() final {
  }
  void on_closed(td::Status) final {
  }
  void on_server_salt_updated() final {
  }
  void on_server_time_difference_updated(bool) final {
  }
  virtual void on_new_session_created(td::uint64, td::mtproto::MessageId) {
  }
  void on_session_failed(td::Status) final {
  }
  void on_container_sent(td::mtproto::MessageId, td::vector<td::mtproto::MessageId>) final {
  }
  td::Status on_pong(double, double, double) final {
    return td::Status::OK();
  }
  td::Status on_update(td::BufferSlice) final {
    return td::Status::OK();
  }
  void on_message_ack(td::mtproto::MessageId) final {
  }
  td::Status on_message_result_ok(td::mtproto::MessageId, td::BufferSlice, size_t) final {
    return td::Status::OK();
  }
  void on_message_result_error(td::mtproto::MessageId, int, td::string) final {
  }
  void on_message_failed(td::mtproto::MessageId, td::Status) final {
  }
  void on_message_info(td::mtproto::MessageId, td::int32, td::mtproto::MessageId, td::int32, td::int32) final {
  }
  td::Status on_destroy_auth_key() final {
    return td::Status::OK();
  }
};

class CapturingSessionCallback final : public NoopSessionCallback {
 public:
  void on_new_session_created(td::uint64 unique_id, td::mtproto::MessageId first_message_id) final {
    seen_events_.push_back({unique_id, first_message_id.get()});
  }

  size_t event_count() const {
    return seen_events_.size();
  }

 private:
  td::vector<std::pair<td::uint64, td::uint64>> seen_events_;
};

void init_auth_data_with_salt(AuthData *auth_data) {
  auth_data->set_session_mode(false);
  auth_data->set_main_auth_key(AuthKey(1, td::string(256, 'a')));
  auth_data->set_server_salt(1, td::Time::now_cached());
  auth_data->set_future_salts({td::mtproto::ServerSalt{2, -1e9, 1e9}}, td::Time::now_cached());
  auth_data->set_session_id(1);
}

TEST(SessionServiceQueryCleanupContract, MsgsStateInfoCompletionReclaimsContainerLookupState) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw = td::make_unique<SendOnlyRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  auto *raw_ptr = raw.get();
  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw), &auth_data);
  NoopSessionCallback callback;

  connection.get_state_info(td::mtproto::MessageId(static_cast<td::uint64>(0x1000)));
  ASSERT_TRUE(connection.flush(&callback) >= 0.0);
  ASSERT_EQ(1, raw_ptr->send_crypto_calls_);
  ASSERT_EQ(1u, SessionConnectionTestPeer::service_query_count(connection));
  ASSERT_EQ(1u, SessionConnectionTestPeer::container_service_mapping_count(connection));

  auto request_message_id = SessionConnectionTestPeer::first_service_query_id(connection);
  auto status = SessionConnectionTestPeer::deliver_msgs_state_info(connection, request_message_id, td::Slice("a"));
  ASSERT_TRUE(status.is_ok());
  ASSERT_EQ(0u, SessionConnectionTestPeer::service_query_count(connection));
  ASSERT_EQ(0u, SessionConnectionTestPeer::container_service_mapping_count(connection));
}

TEST(SessionServiceQueryCleanupContract, ContainerFailureReclaimsCrossReferenceStateAfterReplay) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw = td::make_unique<SendOnlyRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  auto *raw_ptr = raw.get();
  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw), &auth_data);
  NoopSessionCallback callback;

  connection.get_state_info(td::mtproto::MessageId(static_cast<td::uint64>(0x2000)));
  ASSERT_TRUE(connection.flush(&callback) >= 0.0);
  ASSERT_EQ(1, raw_ptr->send_crypto_calls_);
  ASSERT_EQ(1u, SessionConnectionTestPeer::service_query_count(connection));
  ASSERT_EQ(1u, SessionConnectionTestPeer::container_service_mapping_count(connection));

  auto container_message_id = SessionConnectionTestPeer::first_container_message_id(connection);
  SessionConnectionTestPeer::fail_message(connection, container_message_id, td::Status::Error("forced failure"));

  ASSERT_EQ(0u, SessionConnectionTestPeer::service_query_count(connection));
  ASSERT_EQ(0u, SessionConnectionTestPeer::container_service_mapping_count(connection));
}

TEST(SessionServiceQueryCleanupContract, RateGatedNewSessionStillTriggersResendRecoveryCallback) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw = td::make_unique<SendOnlyRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw), &auth_data);
  CapturingSessionCallback callback;
  ASSERT_TRUE(connection.flush(&callback) >= 0.0);

  auto first_message_id = td::mtproto::MessageId(static_cast<td::uint64>(0x3000));
  ASSERT_TRUE(SessionConnectionTestPeer::deliver_new_session_created(connection, 0x1111111111111111ULL, first_message_id)
                  .is_ok());
  ASSERT_TRUE(SessionConnectionTestPeer::deliver_new_session_created(connection, 0x2222222222222222ULL, first_message_id)
                  .is_ok());

  ASSERT_EQ(2u, callback.event_count());
}

}  // namespace
