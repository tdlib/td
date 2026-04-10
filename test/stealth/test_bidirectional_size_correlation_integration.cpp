// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"
#include "td/mtproto/TcpTransport.h"

#include "td/utils/buffer.h"
#include "td/utils/crypto.h"
#include "td/utils/tests.h"

#include <vector>

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::tcp::ObfuscatedTransport;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;

td::string make_tls_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += "www.google.com";
  return secret;
}

td::string aligned_payload(size_t size, char fill) {
  auto aligned_size = (size + 3u) & ~size_t{3};
  return td::string(aligned_size, fill);
}

DrsPhaseModel make_fixed_phase(td::int32 cap) {
  DrsPhaseModel phase;
  phase.bins = {{cap, cap, 1}};
  phase.max_repeat_run = 16;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_size_floor_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_fixed_phase(320);
  config.drs_policy.congestion_open = make_fixed_phase(320);
  config.drs_policy.steady_state = make_fixed_phase(320);
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.min_payload_cap = 320;
  config.drs_policy.max_payload_cap = 320;
  config.bidirectional_correlation_policy.enabled = true;
  config.bidirectional_correlation_policy.small_response_threshold_bytes = 192;
  config.bidirectional_correlation_policy.next_request_min_payload_cap = 1200;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_min = 0.0;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_max = 0.0;
  return config;
}

StealthConfig make_response_jitter_config() {
  MockRng rng(2);
  auto config = make_size_floor_config();
  config.drs_policy.slow_start = make_fixed_phase(512);
  config.drs_policy.congestion_open = make_fixed_phase(512);
  config.drs_policy.steady_state = make_fixed_phase(512);
  config.drs_policy.min_payload_cap = 512;
  config.drs_policy.max_payload_cap = 512;
  config.ipt_params.burst_mu_ms = 0.0;
  config.ipt_params.burst_sigma = 0.0;
  config.ipt_params.burst_max_ms = 1.0;
  config.ipt_params.idle_alpha = 1.0;
  config.ipt_params.idle_scale_ms = 1.0;
  config.ipt_params.idle_max_ms = 2.0;
  config.ipt_params.p_burst_stay = 0.0;
  config.ipt_params.p_idle_to_burst = 0.0;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_min = 11.0;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_max = 11.0;
  return config;
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

td::string take_available_wire(td::ChainBufferReader &reader) {
  reader.sync_with_writer();
  if (reader.empty()) {
    return {};
  }
  return reader.cut_head(reader.size()).move_as_buffer_slice().as_slice().str();
}

td::string make_server_response_wire(td::AesCtrState &&inbound_state, td::Slice payload) {
  td::string frame(payload.size() + 4, '\0');
  auto payload_size = static_cast<td::uint32>(payload.size());
  frame[0] = static_cast<char>(payload_size & 0xff);
  frame[1] = static_cast<char>((payload_size >> 8) & 0xff);
  frame[2] = static_cast<char>((payload_size >> 16) & 0xff);
  frame[3] = static_cast<char>((payload_size >> 24) & 0xff);
  td::MutableSlice(frame.data() + 4, payload.size()).copy_from(payload);

  td::string ciphertext(frame.size(), '\0');
  inbound_state.encrypt(td::Slice(frame), td::MutableSlice(ciphertext));

  td::string wire;
  wire.append("\x14\x03\x03\x00\x01\x01", 6);
  char tls_header[] = "\x17\x03\x03\x00\x00";
  tls_header[3] = static_cast<char>((ciphertext.size() >> 8) & 0xff);
  tls_header[4] = static_cast<char>(ciphertext.size() & 0xff);
  wire.append(tls_header, 5);
  wire.append(ciphertext);
  return wire;
}

struct ClientEndpoint final {
  td::unique_ptr<StealthTransportDecorator> transport;
  ObfuscatedTransport *inner{nullptr};
  MockClock *clock{nullptr};
  td::ChainBufferWriter input_writer;
  td::ChainBufferReader input_reader;
  td::ChainBufferWriter output_writer;
  td::ChainBufferReader output_reader;

  static ClientEndpoint create(StealthConfig config, td::uint64 seed) {
    ClientEndpoint endpoint;
    auto clock = td::make_unique<MockClock>();
    endpoint.clock = clock.get();
    auto inner =
        td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
    endpoint.inner = inner.get();
    auto transport = StealthTransportDecorator::create(std::move(inner), std::move(config),
                                                       td::make_unique<MockRng>(seed), std::move(clock));
    CHECK(transport.is_ok());
    endpoint.transport = transport.move_as_ok();
    endpoint.input_reader = endpoint.input_writer.extract_reader();
    endpoint.output_reader = endpoint.output_writer.extract_reader();
    endpoint.transport->init(&endpoint.input_reader, &endpoint.output_writer);
    return endpoint;
  }
};

void write_and_flush_client(ClientEndpoint &client, td::Slice payload, TrafficHint hint = TrafficHint::Interactive) {
  client.transport->set_traffic_hint(hint);
  td::BufferWriter writer(payload, client.transport->max_prepend_size(), client.transport->max_append_size());
  client.transport->write(std::move(writer), false);
  auto wakeup = client.transport->get_shaping_wakeup();
  if (wakeup > client.clock->now()) {
    client.clock->advance(wakeup - client.clock->now());
  }
  client.transport->pre_flush_write(client.clock->now());
}

td::BufferSlice read_client_message(ClientEndpoint &client) {
  for (int attempt = 0; attempt < 8; attempt++) {
    client.input_reader.sync_with_writer();
    td::BufferSlice message;
    td::uint32 quick_ack = 0;
    auto result = client.transport->read_next(&message, &quick_ack);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(0u, quick_ack);
    if (!message.empty()) {
      return message;
    }
  }
  LOG(FATAL) << "client failed to decode inbound wire message";
  UNREACHABLE();
}

TEST(BidirectionalSizeCorrelationIntegration, RealInboundTlsFramingRaisesNextOutboundRecordFloor) {
  auto client = ClientEndpoint::create(make_size_floor_config(), 17);
  auto warmup_request = aligned_payload(16, 'w');
  auto small_response = aligned_payload(4, 'p');
  auto next_request = aligned_payload(16, 'n');

  write_and_flush_client(client, td::Slice(warmup_request));
  auto warmup_wire = take_available_wire(client.output_reader);
  ASSERT_FALSE(warmup_wire.empty());

  auto inbound_wire =
      make_server_response_wire(client.inner->clone_input_cipher_state_for_tests(), td::Slice(small_response));
  client.input_writer.append(td::Slice(inbound_wire));
  client.input_reader.sync_with_writer();
  auto inbound_response = read_client_message(client);
  ASSERT_TRUE(inbound_response.size() < 192u);

  write_and_flush_client(client, td::Slice(next_request));
  auto outbound_wire = take_available_wire(client.output_reader);
  auto lengths = extract_tls_record_lengths(td::Slice(outbound_wire));
  ASSERT_EQ(1u, lengths.size());
  ASSERT_EQ(1200u, lengths[0]);
}

TEST(BidirectionalSizeCorrelationIntegration, RealInboundTlsFramingArmsPostResponseJitter) {
  auto client = ClientEndpoint::create(make_response_jitter_config(), 29);
  auto warmup_request = aligned_payload(16, 'w');
  auto small_response = aligned_payload(4, 'p');
  auto next_request = aligned_payload(16, 'n');

  write_and_flush_client(client, td::Slice(warmup_request));
  auto warmup_wire = take_available_wire(client.output_reader);
  ASSERT_FALSE(warmup_wire.empty());

  auto inbound_wire =
      make_server_response_wire(client.inner->clone_input_cipher_state_for_tests(), td::Slice(small_response));
  client.input_writer.append(td::Slice(inbound_wire));
  client.input_reader.sync_with_writer();
  auto inbound_response = read_client_message(client);
  ASSERT_TRUE(inbound_response.size() < 192u);

  client.transport->set_traffic_hint(TrafficHint::Interactive);
  td::BufferWriter writer(td::Slice(next_request), client.transport->max_prepend_size(),
                          client.transport->max_append_size());
  client.transport->write(std::move(writer), false);

  auto wakeup = client.transport->get_shaping_wakeup();
  ASSERT_TRUE(wakeup > client.clock->now());
  ASSERT_TRUE(wakeup - client.clock->now() >= 0.011 - 1e-6);
}

}  // namespace