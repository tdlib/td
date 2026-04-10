// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/TlsInitTestHelpers.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/SessionConnection.h"
#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"
#include "td/mtproto/TcpTransport.h"

#include "td/utils/BufferedFd.h"
#include "td/utils/port/PollFlags.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Time.h"
#include "td/utils/tests.h"

#include <vector>

namespace {

using td::mtproto::AuthData;
using td::mtproto::AuthKey;
using td::mtproto::IStreamTransport;
using td::mtproto::ProxySecret;
using td::mtproto::RawConnection;
using td::mtproto::SessionConnection;
using td::mtproto::set_transport_factory_for_tests;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::IRng;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::test::create_socket_pair;
using td::mtproto::test::read_exact;
using td::mtproto::TransportType;
using td::mtproto::StreamTransportFactoryForTests;
using td::mtproto::tcp::ObfuscatedTransport;

constexpr size_t kPrimerHeaderOverhead = 64 + 6;

class DominantBinRng final : public IRng {
 public:
  void fill_secure_bytes(td::MutableSlice dest) final {
    dest.fill('\0');
  }

  td::uint32 secure_uint32() final {
    return 0u;
  }

  td::uint32 bounded(td::uint32 n) final {
    CHECK(n != 0u);
    return 0u;
  }
};

class WriteCapturingStatsCallback final : public RawConnection::StatsCallback {
 public:
  void on_read(td::uint64 bytes) final {
  }

  void on_write(td::uint64 bytes) final {
    writes.push_back(bytes);
  }

  void on_pong() final {
  }

  void on_error() final {
  }

  void on_mtproto_error() final {
  }

  td::vector<td::uint64> writes;
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

td::string make_tls_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += "www.google.com";
  return secret;
}

void init_auth_data_with_salt(AuthData *auth_data) {
  auth_data->set_use_pfs(false);
  auth_data->set_main_auth_key(AuthKey(1, td::string(256, 'a')));
  auth_data->set_server_salt(1, td::Time::now_cached());
  auth_data->set_future_salts({td::mtproto::ServerSalt{2, -1e9, 1e9}}, td::Time::now_cached());
  auth_data->set_session_id(1);
}

void init_auth_data_without_salt(AuthData *auth_data) {
  auth_data->set_use_pfs(false);
  auth_data->set_main_auth_key(AuthKey(1, td::string(256, 'a')));
  auth_data->set_session_id(1);
}

DrsPhaseModel make_phase() {
  DrsPhaseModel phase;
  phase.bins = {{900, 900, 100}, {1200, 1200, 1}};
  phase.max_repeat_run = 2;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_config() {
  DominantBinRng rng;
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase();
  config.drs_policy.congestion_open = make_phase();
  config.drs_policy.steady_state = make_phase();
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 1200;
  return config;
}

td::unique_ptr<IStreamTransport> make_deterministic_stealth_transport(TransportType type) {
  if (type.type != TransportType::ObfuscatedTcp) {
    return nullptr;
  }

  auto decorator = StealthTransportDecorator::create(
      td::make_unique<ObfuscatedTransport>(type.dc_id, type.secret), make_config(), td::make_unique<DominantBinRng>(),
      td::mtproto::stealth::make_clock());
  CHECK(decorator.is_ok());
  return decorator.move_as_ok();
}

std::vector<size_t> extract_tls_record_lengths(td::Slice wire) {
  std::vector<size_t> lengths;
  size_t offset = 0;
  if (wire.size() >= 6 && wire.substr(0, 6) == td::Slice("\x14\x03\x03\x00\x01\x01", 6)) {
    offset = 6;
  }
  while (offset + 5 <= wire.size()) {
    ASSERT_EQ(static_cast<td::uint8>(0x17), static_cast<td::uint8>(wire[offset]));
    ASSERT_EQ(static_cast<td::uint8>(0x03), static_cast<td::uint8>(wire[offset + 1]));
    ASSERT_EQ(static_cast<td::uint8>(0x03), static_cast<td::uint8>(wire[offset + 2]));
    size_t len = (static_cast<td::uint8>(wire[offset + 3]) << 8) | static_cast<td::uint8>(wire[offset + 4]);
    lengths.push_back(len);
    offset += 5 + len;
  }
  ASSERT_EQ(offset, wire.size());
  return lengths;
}

td::int32 decode_first_flush_cap(td::Slice wire, bool primer_expected) {
  auto lengths = extract_tls_record_lengths(wire);
  CHECK(!lengths.empty());
  auto first = lengths[0];
  if (primer_expected) {
    if (first == 900 + kPrimerHeaderOverhead) {
      return 900;
    }
    if (first == 1200 + kPrimerHeaderOverhead) {
      return 1200;
    }
  } else {
    if (first == 900) {
      return 900;
    }
    if (first == 1200) {
      return 1200;
    }
  }
  LOG(FATAL) << "Unexpected first TLS record length " << first << " primer_expected=" << primer_expected;
  UNREACHABLE();
}

struct SessionHarness final {
  td::unique_ptr<SessionConnection> connection;
  td::SocketFd peer;
  WriteCapturingStatsCallback *stats{nullptr};
  NoopSessionCallback callback;
};

td::unique_ptr<SessionHarness> make_harness(AuthData *auth_data) {
  auto socket_pair = create_socket_pair().move_as_ok();
  auto stats_callback = td::make_unique<WriteCapturingStatsCallback>();
  auto *stats_ptr = stats_callback.get();
  auto raw_connection = RawConnection::create(
      td::IPAddress(), td::BufferedFd<td::SocketFd>(std::move(socket_pair.client)),
      TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(make_tls_secret())},
      std::move(stats_callback));

  auto harness = td::make_unique<SessionHarness>();
  harness->connection =
      td::make_unique<SessionConnection>(SessionConnection::Mode::Tcp, std::move(raw_connection), auth_data);
  harness->peer = std::move(socket_pair.peer);
  harness->stats = stats_ptr;
  return harness;
}

td::string flush_and_read(SessionHarness &harness) {
  auto writes_before = harness.stats->writes.size();
  harness.connection->get_poll_info().add_flags(td::PollFlags::Write());
  auto wakeup_at = harness.connection->flush(&harness.callback);
  if (harness.stats->writes.size() == writes_before) {
    CHECK(wakeup_at > td::Time::now_cached());
    td::Time::jump_in_future(wakeup_at + 0.01);
    harness.connection->get_poll_info().add_flags(td::PollFlags::Write());
    static_cast<void>(harness.connection->flush(&harness.callback));
  }
  CHECK(harness.stats->writes.size() == writes_before + 1);
  auto wire = read_exact(harness.peer, static_cast<size_t>(harness.stats->writes.back()));
  CHECK(wire.is_ok());
  return wire.move_as_ok();
}

TEST(SessionRecordSizeSequenceHardeningIntegration, BulkQuerySequencePreservesAntiRepeatOnRealTransportPath) {
  auto previous_factory = set_transport_factory_for_tests(&make_deterministic_stealth_transport);
  SCOPE_EXIT {
    set_transport_factory_for_tests(previous_factory);
  };

  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  auto harness = make_harness(&auth_data);

  std::vector<td::int32> caps;
  for (int index = 0; index < 3; index++) {
    harness->connection->send_query(td::BufferSlice(td::string(9000, 'q')), false, td::mtproto::MessageId(), {}, false);
    caps.push_back(decode_first_flush_cap(flush_and_read(*harness), index == 0));
  }

  ASSERT_EQ((std::vector<td::int32>{900, 900, 1200}), caps);
}

TEST(SessionRecordSizeSequenceHardeningIntegration, KeepaliveFlushDoesNotConsumeBulkSequenceState) {
  auto previous_factory = set_transport_factory_for_tests(&make_deterministic_stealth_transport);
  SCOPE_EXIT {
    set_transport_factory_for_tests(previous_factory);
  };

  AuthData auth_data;
  init_auth_data_with_salt(&auth_data);
  auto harness = make_harness(&auth_data);

  harness->connection->send_query(td::BufferSlice(td::string(9000, 'q')), false, td::mtproto::MessageId(), {}, false);
  auto first_bulk = decode_first_flush_cap(flush_and_read(*harness), true);

  harness->connection->send_query(td::BufferSlice(td::string(9000, 'q')), false, td::mtproto::MessageId(), {}, false);
  auto second_bulk = decode_first_flush_cap(flush_and_read(*harness), false);

  harness->connection->set_online(true, true);
  auto keepalive_cap = decode_first_flush_cap(flush_and_read(*harness), false);

  harness->connection->send_query(td::BufferSlice(td::string(9000, 'q')), false, td::mtproto::MessageId(), {}, false);
  auto third_bulk = decode_first_flush_cap(flush_and_read(*harness), false);

  ASSERT_EQ(900, first_bulk);
  ASSERT_EQ(900, second_bulk);
  ASSERT_EQ(900, keepalive_cap);
  ASSERT_EQ(1200, third_bulk);
}

TEST(SessionRecordSizeSequenceHardeningIntegration, AuthHandshakeFlushDoesNotConsumeBulkSequenceState) {
  auto previous_factory = set_transport_factory_for_tests(&make_deterministic_stealth_transport);
  SCOPE_EXIT {
    set_transport_factory_for_tests(previous_factory);
  };

  AuthData auth_data;
  init_auth_data_without_salt(&auth_data);
  auto harness = make_harness(&auth_data);

  harness->connection->send_query(td::BufferSlice("bootstrap"), false, td::mtproto::MessageId(), {}, false);
  auto handshake_wire = flush_and_read(*harness);
  ASSERT_FALSE(handshake_wire.empty());

  auth_data.set_server_salt(1, td::Time::now_cached());
  auth_data.set_future_salts({td::mtproto::ServerSalt{2, -1e9, 1e9}}, td::Time::now_cached());

  std::vector<td::int32> bulk_caps;
  for (int index = 0; index < 3; index++) {
    harness->connection->send_query(td::BufferSlice(td::string(9000, 'q')), false, td::mtproto::MessageId(), {}, false);
    bulk_caps.push_back(decode_first_flush_cap(flush_and_read(*harness), false));
  }

  ASSERT_EQ((std::vector<td::int32>{900, 900, 1200}), bulk_caps);
}

}  // namespace