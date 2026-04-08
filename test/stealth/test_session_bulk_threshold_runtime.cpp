// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/SessionConnection.h"
#if TDLIB_STEALTH_SHAPING
#include "td/mtproto/stealth/StealthConfig.h"
#endif

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

#if TDLIB_STEALTH_SHAPING
using td::mtproto::create_transport;
using td::mtproto::stealth::IRng;
using td::mtproto::stealth::set_stealth_config_factory_for_tests;
using td::mtproto::stealth::StealthConfig;

td::string make_tls_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += "www.google.com";
  return secret;
}
#endif

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
  HintCapturingRawConnection(td::BufferedFd<td::SocketFd> fd, size_t bulk_threshold_bytes)
      : fd_(std::move(fd)), bulk_threshold_bytes_(bulk_threshold_bytes) {
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

  size_t traffic_bulk_threshold_bytes() const {
    return bulk_threshold_bytes_;
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

 private:
  td::BufferedFd<td::SocketFd> fd_;
  size_t bulk_threshold_bytes_{0};
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
  auth_data->set_use_pfs(false);
  auth_data->set_main_auth_key(AuthKey(1, td::string(256, 'a')));
  auth_data->set_server_salt(1, td::Time::now_cached());
  auth_data->set_future_salts({td::mtproto::ServerSalt{2, -1e9, 1e9}}, td::Time::now_cached());
  auth_data->set_session_id(1);
}

#if TDLIB_STEALTH_SHAPING
td::Result<StealthConfig> custom_bulk_threshold_config_factory(const td::mtproto::ProxySecret &secret, IRng &rng) {
  auto config = StealthConfig::from_secret(secret, rng);
  config.bulk_threshold_bytes = 1337;
  auto status = config.validate();
  if (status.is_error()) {
    return status;
  }
  return config;
}
#endif

TEST(SessionBulkThresholdRuntime, SessionUsesRawConnectionThresholdForBulkSplit) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<HintCapturingRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)), 1024);
  auto *raw_ptr = raw_connection.get();

  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw_connection), &auth_data);
  NoopSessionCallback callback;

  connection.send_query(td::BufferSlice(td::string(2048, 'q')), false, td::mtproto::MessageId(), {}, false);
  connection.flush(&callback);

  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::BulkData, raw_ptr->sent_hints[0]);
}

TEST(SessionBulkThresholdRuntime, SessionKeepsSamePayloadInteractiveAboveCustomThreshold) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection =
      td::make_unique<HintCapturingRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)), 4096);
  auto *raw_ptr = raw_connection.get();

  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw_connection), &auth_data);
  NoopSessionCallback callback;

  connection.send_query(td::BufferSlice(td::string(2048, 'q')), false, td::mtproto::MessageId(), {}, false);
  connection.flush(&callback);

  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, raw_ptr->sent_hints[0]);
}

TEST(SessionBulkThresholdRuntime, StreamTransportExposesRuntimeConfiguredBulkThreshold) {
#if TDLIB_STEALTH_SHAPING
  auto previous_factory = set_stealth_config_factory_for_tests(&custom_bulk_threshold_config_factory);
  SCOPE_EXIT {
    set_stealth_config_factory_for_tests(previous_factory);
  };

  auto transport = create_transport(
      TransportType{TransportType::ObfuscatedTcp, 2, td::mtproto::ProxySecret::from_raw(make_tls_secret())});
  ASSERT_EQ(1337u, transport->traffic_bulk_threshold_bytes());
#else
  ASSERT_TRUE(true);
#endif
}

}  // namespace