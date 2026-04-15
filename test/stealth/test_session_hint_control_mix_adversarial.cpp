// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/SessionConnection.h"
#include "td/mtproto/stealth/Interfaces.h"

#include "td/telegram/net/NetReliabilityMonitor.h"

#include "td/utils/BufferedFd.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::AuthData;
using td::mtproto::AuthKey;
using td::mtproto::RawConnection;
using td::mtproto::SessionConnection;
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

  td::Status flush(const AuthKey &auth_key, Callback &callback) final {
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

class NoopSessionCallback final : public SessionConnection::Callback {
 public:
  void on_connected() final {
  }
  void on_closed(td::Status status) final {
  }
  void on_server_salt_updated() final {
  }
  void on_server_time_difference_updated(bool force) final {
  }
  void on_new_session_created(td::uint64 unique_id, td::mtproto::MessageId first_message_id) final {
  }
  void on_session_failed(td::Status status) final {
  }
  void on_container_sent(td::mtproto::MessageId container_message_id,
                         td::vector<td::mtproto::MessageId> message_ids) final {
  }
  td::Status on_pong(double ping_time, double pong_time, double current_time) final {
    return td::Status::OK();
  }
  td::Status on_update(td::BufferSlice packet) final {
    return td::Status::OK();
  }
  void on_message_ack(td::mtproto::MessageId message_id) final {
  }
  td::Status on_message_result_ok(td::mtproto::MessageId message_id, td::BufferSlice packet,
                                  size_t original_size) final {
    return td::Status::OK();
  }
  void on_message_result_error(td::mtproto::MessageId message_id, int code, td::string message) final {
  }
  void on_message_failed(td::mtproto::MessageId message_id, td::Status status) final {
  }
  void on_message_info(td::mtproto::MessageId message_id, td::int32 state, td::mtproto::MessageId answer_message_id,
                       td::int32 answer_size, td::int32 source) final {
  }
  td::Status on_destroy_auth_key() final {
    return td::Status::OK();
  }
};

void init_auth_data_with_salt(AuthData *auth_data) {
  auth_data->set_session_mode(false);
  auth_data->set_main_auth_key(AuthKey(1, td::string(256, 'a')));
  auth_data->set_server_salt(1, td::Time::now_cached());
  auth_data->set_future_salts({td::mtproto::ServerSalt{2, -1e9, 1e9}}, td::Time::now_cached());
  auth_data->set_session_id(1);
}

TEST(SessionHintControlMixAdversarial, DestroyKeyWithPingStaysInteractive) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<HintCapturingRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  auto *raw_ptr = raw_connection.get();

  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw_connection), &auth_data);
  NoopSessionCallback callback;

  connection.set_online(true, true);
  connection.destroy_key();
  connection.flush(&callback);

  ASSERT_EQ(1, raw_ptr->flush_calls_);
  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, raw_ptr->sent_hints[0]);
}

TEST(SessionHintControlMixAdversarial, DestroyKeyWithPolicyOverrideStaysInteractiveWithoutCoerceCounter) {
  SKIP_IF_NO_SOCKET_PAIR();
  td::net_health::reset_net_monitor_for_tests();

  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<HintCapturingRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  auto *raw_ptr = raw_connection.get();

  AuthData auth_data;
  auth_data.set_session_mode_from_policy(false);
  auth_data.set_main_auth_key(AuthKey(1, td::string(256, 'a')));
  auth_data.set_server_salt(1, td::Time::now_cached());
  auth_data.set_future_salts({td::mtproto::ServerSalt{2, -1e9, 1e9}}, td::Time::now_cached());
  auth_data.set_session_id(1);

  ASSERT_FALSE(auth_data.is_keyed_session());

  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw_connection), &auth_data);
  NoopSessionCallback callback;

  connection.set_online(true, true);
  connection.destroy_key();
  connection.flush(&callback);

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_FALSE(auth_data.is_keyed_session());
  ASSERT_EQ(1, raw_ptr->flush_calls_);
  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, raw_ptr->sent_hints[0]);
  ASSERT_EQ(0u, snapshot.counters.session_param_coerce_attempt_total);
}

TEST(SessionHintControlMixAdversarial, ResendAnswerWithPingStaysInteractive) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<HintCapturingRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  auto *raw_ptr = raw_connection.get();

  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw_connection), &auth_data);
  NoopSessionCallback callback;

  connection.set_online(true, true);
  connection.resend_answer(td::mtproto::MessageId(td::uint64{1}));
  connection.flush(&callback);

  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, raw_ptr->sent_hints[0]);
}

TEST(SessionHintControlMixAdversarial, CancelAnswerWithPingStaysInteractive) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<HintCapturingRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  auto *raw_ptr = raw_connection.get();

  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw_connection), &auth_data);
  NoopSessionCallback callback;

  connection.set_online(true, true);
  connection.cancel_answer(td::mtproto::MessageId(td::uint64{1}));
  connection.flush(&callback);

  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, raw_ptr->sent_hints[0]);
}

TEST(SessionHintControlMixAdversarial, LargeQueryWithDestroyKeyStillUsesBulkData) {
  SKIP_IF_NO_SOCKET_PAIR();
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<HintCapturingRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)));
  auto *raw_ptr = raw_connection.get();

  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw_connection), &auth_data);
  NoopSessionCallback callback;

  connection.destroy_key();
  connection.send_query(td::BufferSlice(td::string(9000, 'q')), false, td::mtproto::MessageId(), {}, false);
  connection.flush(&callback);

  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::BulkData, raw_ptr->sent_hints[0]);
}

}  // namespace