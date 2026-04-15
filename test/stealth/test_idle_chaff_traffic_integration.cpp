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

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::ChaffPolicy;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
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

DrsPhaseModel make_exact_record_model(td::int32 target_bytes) {
  return DrsPhaseModel{{RecordSizeBin{target_bytes, target_bytes, 1}}, 1, 0};
}

StealthConfig make_integration_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_exact_record_model(320);
  config.drs_policy.congestion_open = make_exact_record_model(320);
  config.drs_policy.steady_state = make_exact_record_model(320);
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.min_payload_cap = 320;
  config.drs_policy.max_payload_cap = 320;
  config.ipt_params.burst_mu_ms = 0.0;
  config.ipt_params.burst_sigma = 0.0;
  config.ipt_params.burst_max_ms = 1.0;
  config.ipt_params.idle_alpha = 1.0;
  config.ipt_params.idle_scale_ms = 25.0;
  config.ipt_params.idle_max_ms = 50.0;
  config.ipt_params.p_burst_stay = 0.0;
  config.ipt_params.p_idle_to_burst = 0.0;
  config.chaff_policy = ChaffPolicy{};
  config.chaff_policy.enabled = true;
  config.chaff_policy.idle_threshold_ms = 5000;
  config.chaff_policy.min_interval_ms = 10.0;
  config.chaff_policy.max_bytes_per_minute = 4096;
  config.chaff_policy.record_model = make_exact_record_model(320);
  return config;
}

td::string make_server_response_wire(td::AesCtrState &inbound_state, td::Slice payload, bool include_tls_preamble) {
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
  if (include_tls_preamble) {
    wire.append("\x14\x03\x03\x00\x01\x01", 6);
  }
  char tls_header[] = "\x17\x03\x03\x00\x00";
  tls_header[3] = static_cast<char>((ciphertext.size() >> 8) & 0xff);
  tls_header[4] = static_cast<char>(ciphertext.size() & 0xff);
  wire.append(tls_header, 5);
  wire.append(ciphertext);
  return wire;
}

td::string make_server_quick_ack_wire(td::AesCtrState &inbound_state, td::uint32 quick_ack, bool include_tls_preamble) {
  td::string frame(4, '\0');
  frame[0] = static_cast<char>(quick_ack & 0xff);
  frame[1] = static_cast<char>((quick_ack >> 8) & 0xff);
  frame[2] = static_cast<char>((quick_ack >> 16) & 0xff);
  frame[3] = static_cast<char>((quick_ack >> 24) & 0xff);

  td::string ciphertext(frame.size(), '\0');
  inbound_state.encrypt(td::Slice(frame), td::MutableSlice(ciphertext));

  td::string wire;
  if (include_tls_preamble) {
    wire.append("\x14\x03\x03\x00\x01\x01", 6);
  }
  char tls_header[] = "\x17\x03\x03\x00\x00";
  tls_header[3] = static_cast<char>((ciphertext.size() >> 8) & 0xff);
  tls_header[4] = static_cast<char>(ciphertext.size() & 0xff);
  wire.append(tls_header, 5);
  wire.append(ciphertext);
  return wire;
}

// `ClientEndpoint` is non-movable / non-copyable: the transport's `init`
// captures a raw pointer into `input_reader`, and a static factory
// returning by value would invalidate the captured pointer because MSVC
// Debug builds (and any non-NRVO path) actually copy the locally
// constructed `endpoint` into the caller's stack frame, leaving the
// transport pointing at a freed slot.
class ClientEndpoint final {
 public:
  td::unique_ptr<StealthTransportDecorator> transport;
  ObfuscatedTransport *inner{nullptr};
  MockClock *clock{nullptr};
  td::ChainBufferWriter input_writer;
  td::ChainBufferReader input_reader;
  td::ChainBufferWriter output_writer;
  td::ChainBufferReader output_reader;

  ClientEndpoint(StealthConfig config, td::uint64 seed) {
    auto clock_local = td::make_unique<MockClock>();
    clock = clock_local.get();
    auto inner_local =
        td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
    inner = inner_local.get();
    auto r = StealthTransportDecorator::create(std::move(inner_local), std::move(config),
                                               td::make_unique<MockRng>(seed), std::move(clock_local));
    CHECK(r.is_ok());
    transport = r.move_as_ok();
    input_reader = input_writer.extract_reader();
    output_reader = output_writer.extract_reader();
    transport->init(&input_reader, &output_writer);
  }

  ClientEndpoint(const ClientEndpoint &) = delete;
  ClientEndpoint &operator=(const ClientEndpoint &) = delete;
  ClientEndpoint(ClientEndpoint &&) = delete;
  ClientEndpoint &operator=(ClientEndpoint &&) = delete;
};

td::BufferSlice read_client_message(ClientEndpoint &client) {
  client.input_reader.sync_with_writer();
  td::BufferSlice message;
  td::uint32 quick_ack = 0;
  auto result = client.transport->read_next(&message, &quick_ack);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(0u, quick_ack);
  return message;
}

td::uint32 read_client_quick_ack(ClientEndpoint &client) {
  client.input_reader.sync_with_writer();
  td::BufferSlice message;
  td::uint32 quick_ack = 0;
  auto result = client.transport->read_next(&message, &quick_ack);
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(message.empty());
  return quick_ack;
}

TEST(IdleChaffTrafficIntegration, ZeroLengthFrameLeavesReceiverReadyForNextRealMessage) {
  ClientEndpoint client(make_integration_config(), 17);
  auto cipher_state = client.inner->clone_input_cipher_state_for_tests();
  auto empty_wire = make_server_response_wire(cipher_state, td::Slice(), true);
  auto next_payload = aligned_payload(12, 'n');
  auto next_wire = make_server_response_wire(cipher_state, td::Slice(next_payload), false);

  client.input_writer.append(td::Slice(empty_wire));
  client.input_reader.sync_with_writer();
  auto empty_message = read_client_message(client);
  ASSERT_TRUE(empty_message.empty());

  client.input_writer.append(td::Slice(next_wire));
  client.input_reader.sync_with_writer();
  auto decoded_message = read_client_message(client);
  ASSERT_EQ(td::Slice(next_payload), decoded_message.as_slice());
}

TEST(IdleChaffTrafficIntegration, InboundTrafficRearmsIdleThresholdBeforeNextChaff) {
  ClientEndpoint client(make_integration_config(), 23);
  auto initial_wakeup = client.transport->get_shaping_wakeup();
  ASSERT_TRUE(initial_wakeup >= client.clock->now() + 5.0 - 1e-6);

  client.clock->advance(4.0);
  auto cipher_state = client.inner->clone_input_cipher_state_for_tests();
  auto inbound_payload = aligned_payload(8, 'r');
  auto inbound_wire = make_server_response_wire(cipher_state, td::Slice(inbound_payload), true);
  client.input_writer.append(td::Slice(inbound_wire));
  auto decoded_message = read_client_message(client);
  ASSERT_EQ(td::Slice(inbound_payload), decoded_message.as_slice());

  auto rearmed_wakeup = client.transport->get_shaping_wakeup();
  ASSERT_TRUE(rearmed_wakeup >= client.clock->now() + 5.0 - 1e-6);
  ASSERT_TRUE(rearmed_wakeup > initial_wakeup);
}

TEST(IdleChaffTrafficIntegration, ZeroLengthInboundFrameRearmsIdleThresholdBeforeNextChaff) {
  ClientEndpoint client(make_integration_config(), 29);
  auto initial_wakeup = client.transport->get_shaping_wakeup();
  ASSERT_TRUE(initial_wakeup >= client.clock->now() + 5.0 - 1e-6);

  client.clock->advance(4.0);
  auto cipher_state = client.inner->clone_input_cipher_state_for_tests();
  auto empty_wire = make_server_response_wire(cipher_state, td::Slice(), true);
  client.input_writer.append(td::Slice(empty_wire));
  auto empty_message = read_client_message(client);
  ASSERT_TRUE(empty_message.empty());

  auto rearmed_wakeup = client.transport->get_shaping_wakeup();
  ASSERT_TRUE(rearmed_wakeup >= client.clock->now() + 5.0 - 1e-6);
  ASSERT_TRUE(rearmed_wakeup > initial_wakeup);
}

TEST(IdleChaffTrafficIntegration, QuickAckRearmsIdleThresholdBeforeNextChaff) {
  ClientEndpoint client(make_integration_config(), 31);
  auto initial_wakeup = client.transport->get_shaping_wakeup();
  ASSERT_TRUE(initial_wakeup >= client.clock->now() + 5.0 - 1e-6);

  client.clock->advance(4.0);
  auto cipher_state = client.inner->clone_input_cipher_state_for_tests();
  constexpr td::uint32 kQuickAck = 0x80000011u;
  auto quick_ack_wire = make_server_quick_ack_wire(cipher_state, kQuickAck, true);
  client.input_writer.append(td::Slice(quick_ack_wire));
  ASSERT_EQ(kQuickAck, read_client_quick_ack(client));

  auto rearmed_wakeup = client.transport->get_shaping_wakeup();
  ASSERT_TRUE(rearmed_wakeup >= client.clock->now() + 5.0 - 1e-6);
  ASSERT_TRUE(rearmed_wakeup > initial_wakeup);
}

}  // namespace