//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/SessionConnection.h"

#include "td/utils/BufferedFd.h"
#include "td/utils/tests.h"
#include "td/utils/Time.h"

namespace {

using td::mtproto::AuthData;
using td::mtproto::AuthKey;
using td::mtproto::RawConnection;
using td::mtproto::SessionConnection;
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

class FakeRawConnection final : public RawConnection {
 public:
  FakeRawConnection(td::BufferedFd<td::SocketFd> fd, double shaping_wakeup)
      : fd_(std::move(fd)), shaping_wakeup_(shaping_wakeup) {
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
                     td::uint64 quick_ack_token) final {
    return 0;
  }

  void send_no_crypto(const td::Storer &storer) final {
  }

  td::PollableFdInfo &get_poll_info() final {
    return fd_.get_poll_info();
  }

  StatsCallback *stats_callback() final {
    return &stats_callback_;
  }

  double shaping_wakeup_at() const final {
    return shaping_wakeup_;
  }

  td::Status flush(const AuthKey &auth_key, Callback &callback) final {
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

  int flush_calls() const {
    return flush_calls_;
  }

 private:
  td::BufferedFd<td::SocketFd> fd_;
  double shaping_wakeup_{0.0};
  int flush_calls_{0};
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

TEST(SessionWakeupOverdueShaping, FlushReturnsOverdueRawConnectionShapingWakeupInsteadOfLaterTimeouts) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto shaping_wakeup = td::Time::now_cached() - 0.125;
  auto raw_connection =
      td::make_unique<FakeRawConnection>(td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)), shaping_wakeup);
  auto *raw_ptr = raw_connection.get();

  AuthData auth_data;
  auth_data.set_use_pfs(false);
  auth_data.set_main_auth_key(AuthKey(1, td::string(256, 'a')));
  auth_data.set_server_salt(1, td::Time::now_cached());
  auth_data.set_session_id(1);

  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw_connection), &auth_data);
  NoopSessionCallback callback;

  auto wakeup_at = connection.flush(&callback);
  ASSERT_EQ(shaping_wakeup, wakeup_at);
  ASSERT_TRUE(wakeup_at < td::Time::now_cached());
  ASSERT_EQ(1, raw_ptr->flush_calls());
}

}  // namespace