// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/mtproto_api.h"
#include "td/mtproto/PacketInfo.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/SessionConnection.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/utils.h"

#include "td/utils/BufferedFd.h"
#include "td/utils/tests.h"
#include "td/utils/tl_storers.h"

namespace {

using td::mtproto::AuthData;
using td::mtproto::AuthKey;
using td::mtproto::PacketInfo;
using td::mtproto::RawConnection;
using td::mtproto::SessionConnection;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::create_socket_pair;
using td::mtproto::TransportType;
using td::TLObjectStorer;
using td::TlStorerUnsafe;

constexpr td::int32 kMsgContainerId = 0x73f1f8dc;

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

class ScriptedInboundRawConnection final : public RawConnection {
 public:
  struct InboundPacket final {
    PacketInfo packet_info;
    td::BufferSlice packet;
  };

  ScriptedInboundRawConnection(td::BufferedFd<td::SocketFd> fd, size_t bulk_threshold_bytes)
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

  td::PollableFdInfo &get_poll_info() final {
    return fd_.get_poll_info();
  }

  StatsCallback *stats_callback() final {
    return &stats_callback_;
  }

  double shaping_wakeup_at() const final {
    return 0.0;
  }

  size_t traffic_bulk_threshold_bytes() const final {
    return bulk_threshold_bytes_;
  }

  td::Status flush(const AuthKey &auth_key, Callback &callback) final {
    (void)auth_key;
    flush_calls_++;
    if (next_step_ < scripted_steps_.size()) {
      for (const auto &packet : scripted_steps_[next_step_]) {
        TRY_STATUS(callback.on_raw_packet(packet.packet_info, packet.packet.copy()));
      }
      next_step_++;
    }
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

  void add_scripted_step(td::vector<InboundPacket> packets) {
    scripted_steps_.push_back(std::move(packets));
  }

  td::vector<TrafficHint> sent_hints;
  int flush_calls_{0};

 private:
  td::BufferedFd<td::SocketFd> fd_;
  size_t bulk_threshold_bytes_{0};
  td::vector<td::vector<InboundPacket>> scripted_steps_;
  size_t next_step_{0};
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

template <class T>
td::BufferSlice store_tl_object(const T &object) {
  TLObjectStorer<T> storer(object);
  td::BufferSlice result(storer.size());
  storer.store(result.as_mutable_slice().ubegin());
  return result;
}

td::BufferSlice make_wire_message(td::uint64 message_id, td::int32 seq_no, td::Slice body) {
  td::BufferSlice result(sizeof(td::int64) + sizeof(td::int32) + sizeof(td::int32) + body.size());
  TlStorerUnsafe storer(result.as_mutable_slice().ubegin());
  storer.store_long(static_cast<td::int64>(message_id));
  storer.store_int(seq_no);
  storer.store_int(static_cast<td::int32>(body.size()));
  storer.store_slice(body);
  return result;
}

td::BufferSlice make_msgs_ack_message(td::uint64 message_id, td::int32 seq_no) {
  td::mtproto_api::msgs_ack object(td::mtproto_api::array<td::int64>{});
  auto body = store_tl_object(object);
  return make_wire_message(message_id, seq_no, body.as_slice());
}

td::BufferSlice make_container_body(const td::vector<td::BufferSlice> &nested_messages) {
  size_t body_size = sizeof(td::int32);
  for (const auto &message : nested_messages) {
    body_size += message.size();
  }

  td::BufferSlice result(body_size);
  TlStorerUnsafe storer(result.as_mutable_slice().ubegin());
  storer.store_int(static_cast<td::int32>(nested_messages.size()));
  for (const auto &message : nested_messages) {
    storer.store_slice(message.as_slice());
  }
  return result;
}

td::BufferSlice make_container_message(td::uint64 container_message_id, td::int32 seq_no,
                                       const td::vector<td::BufferSlice> &nested_messages) {
  auto container_body = make_container_body(nested_messages);
  td::BufferSlice object(sizeof(td::int32) + container_body.size());
  TlStorerUnsafe storer(object.as_mutable_slice().ubegin());
  storer.store_int(kMsgContainerId);
  storer.store_slice(container_body.as_slice());
  return make_wire_message(container_message_id, seq_no, object.as_slice());
}

td::uint64 make_server_message_id(td::uint64 base, size_t ordinal) {
  return base + static_cast<td::uint64>(ordinal) * 4;
}

ScriptedInboundRawConnection::InboundPacket make_ack_container_packet(td::uint64 session_id, td::uint64 base_message_id,
                                                                      size_t nested_count) {
  td::vector<td::BufferSlice> nested_messages;
  nested_messages.reserve(nested_count);
  for (size_t i = 0; i < nested_count; i++) {
    nested_messages.push_back(make_msgs_ack_message(make_server_message_id(base_message_id, i + 1), 1));
  }

  auto container_message = make_container_message(make_server_message_id(base_message_id, 0), 1, nested_messages);

  PacketInfo packet_info;
  packet_info.version = 2;
  packet_info.no_crypto_flag = false;
  packet_info.session_id = session_id;
  packet_info.message_id = td::mtproto::MessageId(make_server_message_id(base_message_id, 0));
  packet_info.seq_no = 1;

  return ScriptedInboundRawConnection::InboundPacket{packet_info, std::move(container_message)};
}

td::vector<ScriptedInboundRawConnection::InboundPacket> make_single_packet_step(
    ScriptedInboundRawConnection::InboundPacket packet) {
  td::vector<ScriptedInboundRawConnection::InboundPacket> result;
  result.push_back(std::move(packet));
  return result;
}

td::uint64 fresh_base_message_id() {
  return (static_cast<td::uint64>(td::Time::now_cached() * (static_cast<td::uint64>(1) << 32)) | 1u);
}

constexpr size_t kCustomBulkThresholdBytes = 1025;
constexpr size_t kNestedAckCountBelowCeilBoundary = 127;
constexpr size_t kNestedAckCountAtCeilBoundary = 128;

TEST(SessionAckThresholdRuntimeIntegration, AckBurstBelowCeilBoundaryStaysKeepalive) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection = td::make_unique<ScriptedInboundRawConnection>(
      td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)), kCustomBulkThresholdBytes);
  auto *raw_ptr = raw_connection.get();

  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  raw_ptr->add_scripted_step(make_single_packet_step(make_ack_container_packet(
      auth_data.get_session_id(), fresh_base_message_id(), kNestedAckCountBelowCeilBoundary)));

  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw_connection), &auth_data);
  NoopSessionCallback callback;

  connection.set_online(true, true);
  connection.flush(&callback);

  ASSERT_EQ(1, raw_ptr->flush_calls_);
  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, raw_ptr->sent_hints[0]);
}

TEST(SessionAckThresholdRuntimeIntegration, AckBurstAtCeilBoundaryEscalatesToBulkData) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection = td::make_unique<ScriptedInboundRawConnection>(
      td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)), kCustomBulkThresholdBytes);
  auto *raw_ptr = raw_connection.get();

  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  raw_ptr->add_scripted_step(make_single_packet_step(
      make_ack_container_packet(auth_data.get_session_id(), fresh_base_message_id(), kNestedAckCountAtCeilBoundary)));

  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw_connection), &auth_data);
  NoopSessionCallback callback;

  connection.set_online(true, true);
  connection.flush(&callback);

  ASSERT_EQ(1, raw_ptr->flush_calls_);
  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::BulkData, raw_ptr->sent_hints[0]);
}

TEST(SessionAckThresholdRuntimeIntegration, SmallUserQueryBelowAckBoundaryStaysInteractive) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto raw_connection = td::make_unique<ScriptedInboundRawConnection>(
      td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)), kCustomBulkThresholdBytes);
  auto *raw_ptr = raw_connection.get();

  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  raw_ptr->add_scripted_step(make_single_packet_step(make_ack_container_packet(
      auth_data.get_session_id(), fresh_base_message_id(), kNestedAckCountBelowCeilBoundary)));

  SessionConnection connection(SessionConnection::Mode::Tcp, std::move(raw_connection), &auth_data);
  NoopSessionCallback callback;

  connection.set_online(true, true);
  connection.send_query(td::BufferSlice(td::string(256, 'q')), false, td::mtproto::MessageId(), {}, false);
  connection.flush(&callback);

  ASSERT_EQ(1, raw_ptr->flush_calls_);
  ASSERT_EQ(1u, raw_ptr->sent_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, raw_ptr->sent_hints[0]);
}

}  // namespace