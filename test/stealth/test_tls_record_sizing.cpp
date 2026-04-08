// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/TcpTransport.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <limits>
#include <vector>

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::tcp::ObfuscatedTransport;

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

td::string flush_transport_write(ObfuscatedTransport &transport, size_t payload_size) {
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

TEST(TlsRecordSizing, RuntimeSetterAppliesToTlsRecordSplitting) {
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(512);

  auto wire = flush_transport_write(transport, 4096);
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() > 1u);
  for (auto len : lengths) {
    ASSERT_TRUE(len <= 512u);
  }
}

TEST(TlsRecordSizing, RecordSizeSetterClampsBelowTlsSafeMinimum) {
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(64);

  auto wire = flush_transport_write(transport, 1024);
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() > 1u);
  for (auto len : lengths) {
    ASSERT_TRUE(len <= 256u);
  }
}

TEST(TlsRecordSizing, RecordSizeSetterClampsHostileExtremeInputs) {
  {
    ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
    transport.set_max_tls_record_size(std::numeric_limits<td::int32>::min());

    auto wire = flush_transport_write(transport, 1024);
    auto lengths = extract_tls_record_lengths(wire);
    ASSERT_TRUE(lengths.size() > 1u);
    for (auto len : lengths) {
      ASSERT_TRUE(len <= 256u);
    }
  }

  {
    ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
    transport.set_max_tls_record_size(std::numeric_limits<td::int32>::max());

    auto wire = flush_transport_write(transport, 20000);
    auto lengths = extract_tls_record_lengths(wire);
    ASSERT_TRUE(lengths.size() > 1u);
    for (auto len : lengths) {
      ASSERT_TRUE(len <= 16384u);
    }
  }
}

TEST(TlsRecordSizing, CapabilityGuardTracksTlsEmulationOnly) {
  ObfuscatedTransport tls_transport(2, ProxySecret::from_raw(make_tls_secret()));
  ASSERT_TRUE(tls_transport.supports_tls_record_sizing());

  ObfuscatedTransport plain_transport(2, ProxySecret::from_raw("0123456789abcdef"));
  ASSERT_FALSE(plain_transport.supports_tls_record_sizing());
}

TEST(TlsRecordSizing, FirstTlsWriteOverheadIncludesPrimerAndThenDropsToZero) {
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  auto first_overhead = transport.tls_record_sizing_payload_overhead();
  ASSERT_EQ(70, first_overhead);

  td::BufferWriter writer(td::Slice("xxxx"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer), false);

  ASSERT_EQ(0, transport.tls_record_sizing_payload_overhead());
}

}  // namespace