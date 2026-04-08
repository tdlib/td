// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/HttpTransport.h"
#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/stealth/StealthConfig.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

#include <vector>

namespace {

using td::mtproto::create_transport;
using td::mtproto::ProxySecret;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::TransportType;

#if TDLIB_STEALTH_SHAPING
using td::mtproto::stealth::IRng;
using td::mtproto::stealth::set_stealth_config_factory_for_tests;
using td::mtproto::stealth::StealthConfig;

td::Result<StealthConfig> fail_transport_stealth_config(const ProxySecret &secret, IRng &rng) {
  (void)secret;
  (void)rng;
  return td::Status::Error("forced invalid stealth config for test");
}
#endif

td::string make_tls_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += "www.google.com";
  return secret;
}

std::vector<size_t> extract_tls_record_lengths(td::Slice wire) {
  std::vector<size_t> lengths;
  size_t offset = 0;
  if (wire.size() >= 6 && wire.substr(0, 6) == td::Slice("\x14\x03\x03\x00\x01\x01", 6)) {
    offset = 6;
  }
  while (offset + 5 <= wire.size()) {
    size_t len = (static_cast<td::uint8>(wire[offset + 3]) << 8) | static_cast<td::uint8>(wire[offset + 4]);
    lengths.push_back(len);
    offset += 5 + len;
  }
  return lengths;
}

td::string flush_transport_write(td::mtproto::IStreamTransport &transport, size_t payload_size) {
  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  td::BufferWriter writer(td::Slice(td::string(payload_size, 'x')), transport.max_prepend_size(),
                          transport.max_append_size());
  transport.write(std::move(writer), false);

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  return output_reader.move_as_buffer_slice().as_slice().str();
}

TEST(StreamTransportSeam, CreateTransportPreservesLegacyTransportKinds) {
  auto tcp = create_transport(TransportType{TransportType::Tcp, 0, ProxySecret()});
  ASSERT_EQ(TransportType::Tcp, tcp->get_type().type);

  auto http = create_transport(TransportType{TransportType::Http, 0, ProxySecret::from_raw("example.com")});
  ASSERT_EQ(TransportType::Http, http->get_type().type);
}

TEST(StreamTransportSeam, ObfuscatedTcpWithoutTlsEmulationStaysUndecorated) {
  auto transport =
      create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw("dd1234567890abcde")});
  ASSERT_EQ(TransportType::ObfuscatedTcp, transport->get_type().type);
  ASSERT_FALSE(transport->supports_tls_record_sizing());
}

TEST(StreamTransportSeam, TlsEmulationUsesCompileTimeActivationGate) {
  auto transport =
      create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(make_tls_secret())});
  auto wire = flush_transport_write(*transport, 4096);
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_FALSE(lengths.empty());
#if TDLIB_STEALTH_SHAPING
  for (auto len : lengths) {
    ASSERT_TRUE(len <= 1460u);
  }
#else
  ASSERT_TRUE(lengths[0] > 1460u);
#endif
}

TEST(StreamTransportSeam, InvalidRuntimeStealthConfigFailsClosedToInnerTransport) {
#if TDLIB_STEALTH_SHAPING
  auto previous_factory = set_stealth_config_factory_for_tests(&fail_transport_stealth_config);
  SCOPE_EXIT {
    set_stealth_config_factory_for_tests(previous_factory);
  };
#endif

  auto transport =
      create_transport(TransportType{TransportType::ObfuscatedTcp, 2, ProxySecret::from_raw(make_tls_secret())});
  auto wire = flush_transport_write(*transport, 4096);
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_FALSE(lengths.empty());
  ASSERT_TRUE(transport->supports_tls_record_sizing());

  ASSERT_TRUE(lengths[0] > 1460u);
}

TEST(StreamTransportSeam, DefaultSeamMethodsRemainSafeNoOps) {
  auto tcp = create_transport(TransportType{TransportType::Tcp, 0, ProxySecret()});
  tcp->pre_flush_write(123.0);
  tcp->set_traffic_hint(TrafficHint::Keepalive);
  tcp->set_max_tls_record_size(999);
  ASSERT_EQ(0.0, tcp->get_shaping_wakeup());
  ASSERT_FALSE(tcp->supports_tls_record_sizing());

  auto http = create_transport(TransportType{TransportType::Http, 0, ProxySecret::from_raw("example.com")});
  http->pre_flush_write(456.0);
  http->set_traffic_hint(TrafficHint::Interactive);
  http->set_max_tls_record_size(2048);
  ASSERT_EQ(0.0, http->get_shaping_wakeup());
  ASSERT_FALSE(http->supports_tls_record_sizing());
}

}  // namespace