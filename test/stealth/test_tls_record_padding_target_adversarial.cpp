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
#include <optional>
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

td::string write_tls_sequence(ObfuscatedTransport &transport, td::Slice primer_payload, td::Slice payload,
                              td::Slice trailing_payload = td::Slice(),
                              std::optional<td::int32> target_after_primer = std::nullopt) {
  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  td::BufferWriter primer(primer_payload, transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(primer), false);

  if (target_after_primer.has_value()) {
    transport.set_stealth_record_padding_target(*target_after_primer);
  }

  td::BufferWriter writer(payload, transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer), false);

  if (!trailing_payload.empty()) {
    td::BufferWriter trailing(trailing_payload, transport.max_prepend_size(), transport.max_append_size());
    transport.write(std::move(trailing), false);
  }

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  return output_reader.move_as_buffer_slice().as_slice().str();
}

TEST(TlsRecordPaddingTargetAdversarial, RequestedTargetIsClampedByTlsRecordCap) {
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(512);

  auto wire = write_tls_sequence(transport, td::Slice("warm"), td::Slice("tiny"), td::Slice(), 1400);
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  ASSERT_EQ(512u, lengths[1]);
}

TEST(TlsRecordPaddingTargetAdversarial, PaddingTargetBelowPayloadDoesNotUnderflow) {
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);

  td::string payload(1024, 'x');
  auto wire = write_tls_sequence(transport, td::Slice("warm"), td::Slice(payload), td::Slice(), 256);
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  ASSERT_TRUE(lengths[1] > 256u);
  ASSERT_TRUE(lengths[1] < 1100u);
}

TEST(TlsRecordPaddingTargetAdversarial, PaddingTargetAppliesToNextWriteOnly) {
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);

  auto wire = write_tls_sequence(transport, td::Slice("warm"), td::Slice("tiny"), td::Slice("next"), 1400);
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 3u);
  ASSERT_EQ(1400u, lengths[1]);
  ASSERT_TRUE(lengths[2] < 256u);
}

TEST(TlsRecordPaddingTargetAdversarial, NegativePadTargetFailsClosedToNativeSize) {
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);

  auto wire = write_tls_sequence(transport, td::Slice("warm"), td::Slice("tiny"), td::Slice(),
                                 std::numeric_limits<td::int32>::min());
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  ASSERT_TRUE(lengths[1] < 256u);
}

TEST(TlsRecordPaddingTargetAdversarial, OversizedPadTargetSaturatesAtTlsMaximum) {
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);

  auto wire = write_tls_sequence(transport, td::Slice("warm"), td::Slice("tiny"), td::Slice(),
                                 std::numeric_limits<td::int32>::max());
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  ASSERT_EQ(16384u, lengths[1]);
}

}  // namespace