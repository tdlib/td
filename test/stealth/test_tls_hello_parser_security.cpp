//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::parse_tls_client_hello;

td::string build_ech_enabled_client_hello(td::int32 unix_time) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;
  return build_default_tls_client_hello("www.google.com", "0123456789secret", unix_time, hints);
}

struct HelloOffsets final {
  size_t handshake_type_offset{0};
  size_t compression_methods_offset{0};
  size_t extensions_length_offset{0};
  size_t extensions_start{0};
  size_t extensions_end{0};
};

td::uint16 read_u16(td::Slice data, size_t offset) {
  CHECK(offset + 2 <= data.size());
  return static_cast<td::uint16>((static_cast<td::uint8>(data[offset]) << 8) |
                                 static_cast<td::uint8>(data[offset + 1]));
}

td::uint32 read_u24(td::Slice data, size_t offset) {
  CHECK(offset + 3 <= data.size());
  return (static_cast<td::uint32>(static_cast<td::uint8>(data[offset])) << 16) |
         (static_cast<td::uint32>(static_cast<td::uint8>(data[offset + 1])) << 8) |
         static_cast<td::uint32>(static_cast<td::uint8>(data[offset + 2]));
}

void write_u16(td::MutableSlice data, size_t offset, td::uint16 value) {
  CHECK(offset + 2 <= data.size());
  data[offset] = static_cast<char>((value >> 8) & 0xff);
  data[offset + 1] = static_cast<char>(value & 0xff);
}

void write_u24(td::MutableSlice data, size_t offset, td::uint32 value) {
  CHECK(offset + 3 <= data.size());
  data[offset] = static_cast<char>((value >> 16) & 0xff);
  data[offset + 1] = static_cast<char>((value >> 8) & 0xff);
  data[offset + 2] = static_cast<char>(value & 0xff);
}

HelloOffsets get_hello_offsets(td::Slice wire) {
  CHECK(wire.size() >= 48);

  HelloOffsets offsets;
  offsets.handshake_type_offset = 5;

  size_t pos = 9;  // after record(5) + handshake header(4)
  pos += 2;        // client legacy version
  pos += 32;       // random

  CHECK(pos < wire.size());
  auto session_id_len = static_cast<size_t>(static_cast<td::uint8>(wire[pos]));
  pos += 1;
  pos += session_id_len;

  auto cipher_suites_len = static_cast<size_t>(read_u16(wire, pos));
  pos += 2;
  pos += cipher_suites_len;

  CHECK(pos < wire.size());
  auto compression_methods_len = static_cast<size_t>(static_cast<td::uint8>(wire[pos]));
  pos += 1;
  offsets.compression_methods_offset = pos;
  pos += compression_methods_len;

  offsets.extensions_length_offset = pos;
  auto extensions_len = static_cast<size_t>(read_u16(wire, pos));
  pos += 2;
  offsets.extensions_start = pos;
  offsets.extensions_end = pos + extensions_len;

  CHECK(offsets.extensions_end <= wire.size());
  return offsets;
}

void update_hello_length_headers(td::MutableSlice wire, size_t added_bytes) {
  auto record_len = static_cast<size_t>(read_u16(wire, 3));
  auto handshake_len = static_cast<size_t>(read_u24(wire, 6));

  record_len += added_bytes;
  handshake_len += added_bytes;

  CHECK(record_len <= 0xFFFFu);
  CHECK(handshake_len <= 0xFFFFFFu);

  write_u16(wire, 3, static_cast<td::uint16>(record_len));
  write_u24(wire, 6, static_cast<td::uint32>(handshake_len));
}

bool duplicate_extension(td::string &wire, td::uint16 ext_type) {
  auto offsets = get_hello_offsets(wire);
  td::Slice view(wire);

  size_t pos = offsets.extensions_start;
  td::string ext_blob;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto current_type = read_u16(view, pos);
    auto current_len = static_cast<size_t>(read_u16(view, pos + 2));
    auto ext_total = 4 + current_len;
    CHECK(pos + ext_total <= offsets.extensions_end);

    if (current_type == ext_type) {
      ext_blob = wire.substr(pos, ext_total);
      break;
    }
    pos += ext_total;
  }

  if (ext_blob.empty()) {
    return false;
  }

  wire.insert(offsets.extensions_end, ext_blob);

  auto mutable_wire = td::MutableSlice(wire);
  auto old_extensions_len = static_cast<size_t>(read_u16(mutable_wire, offsets.extensions_length_offset));
  auto new_extensions_len = old_extensions_len + ext_blob.size();
  CHECK(new_extensions_len <= 0xFFFFu);
  write_u16(mutable_wire, offsets.extensions_length_offset, static_cast<td::uint16>(new_extensions_len));
  update_hello_length_headers(mutable_wire, ext_blob.size());
  return true;
}

bool duplicate_supported_group(td::string &wire) {
  auto offsets = get_hello_offsets(wire);
  td::MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    CHECK(ext_value_start + ext_len <= offsets.extensions_end);

    if (ext_type == 0x000A && ext_len >= 8) {
      auto groups_len = static_cast<size_t>(read_u16(mutable_wire, ext_value_start));
      if (groups_len >= 4) {
        auto first_group = read_u16(mutable_wire, ext_value_start + 2);
        write_u16(mutable_wire, ext_value_start + 4, first_group);
        return true;
      }
    }

    pos += 4 + ext_len;
  }
  return false;
}

bool duplicate_key_share_group(td::string &wire) {
  auto offsets = get_hello_offsets(wire);
  td::MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    CHECK(ext_value_start + ext_len <= offsets.extensions_end);

    if (ext_type == 0x0033 && ext_len >= 2) {
      auto shares_len = static_cast<size_t>(read_u16(mutable_wire, ext_value_start));
      size_t share_pos = ext_value_start + 2;
      size_t share_end = share_pos + shares_len;
      if (share_end > ext_value_start + ext_len) {
        return false;
      }

      // Find second and third entries and make third duplicate the second group id.
      size_t entry_index = 0;
      size_t second_entry_group_offset = 0;
      size_t third_entry_group_offset = 0;
      while (share_pos < share_end) {
        if (share_pos + 4 > share_end) {
          return false;
        }
        auto key_len = static_cast<size_t>(read_u16(mutable_wire, share_pos + 2));
        auto next_share = share_pos + 4 + key_len;
        if (next_share > share_end) {
          return false;
        }

        entry_index++;
        if (entry_index == 2) {
          second_entry_group_offset = share_pos;
        } else if (entry_index == 3) {
          third_entry_group_offset = share_pos;
          break;
        }

        share_pos = next_share;
      }

      if (second_entry_group_offset == 0 || third_entry_group_offset == 0) {
        return false;
      }

      auto second_group = read_u16(mutable_wire, second_entry_group_offset);
      write_u16(mutable_wire, third_entry_group_offset, second_group);
      return true;
    }

    pos += 4 + ext_len;
  }

  return false;
}

bool mutate_ech_value_prefix(td::string &wire, td::uint8 outer, td::uint16 kdf_id, td::uint16 aead_id) {
  auto offsets = get_hello_offsets(wire);
  td::MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    CHECK(ext_value_start + ext_len <= offsets.extensions_end);

    if (ext_type == 0xFE0D) {
      if (ext_len < 8) {
        return false;
      }
      mutable_wire[ext_value_start + 0] = static_cast<char>(outer);
      write_u16(mutable_wire, ext_value_start + 1, kdf_id);
      write_u16(mutable_wire, ext_value_start + 3, aead_id);
      return true;
    }

    pos += 4 + ext_len;
  }

  return false;
}

bool set_ech_declared_enc_length(td::string &wire, td::uint16 enc_length) {
  auto offsets = get_hello_offsets(wire);
  td::MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    CHECK(ext_value_start + ext_len <= offsets.extensions_end);

    if (ext_type == 0xFE0D) {
      if (ext_len < 8) {
        return false;
      }
      write_u16(mutable_wire, ext_value_start + 6, enc_length);
      return true;
    }

    pos += 4 + ext_len;
  }

  return false;
}

bool set_ech_payload_length(td::string &wire, td::uint16 payload_length) {
  auto offsets = get_hello_offsets(wire);
  td::MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    CHECK(ext_value_start + ext_len <= offsets.extensions_end);

    if (ext_type == 0xFE0D) {
      if (ext_len < 10) {
        return false;
      }

      // ECH layout in parser: 1 outer + 2 kdf + 2 aead + 1 config + 2 enc_len + enc + 2 payload_len + payload.
      auto enc_len = static_cast<size_t>(read_u16(mutable_wire, ext_value_start + 6));
      auto payload_len_offset = ext_value_start + 8 + enc_len;
      if (payload_len_offset + 2 > ext_value_start + ext_len) {
        return false;
      }
      write_u16(mutable_wire, payload_len_offset, payload_length);
      return true;
    }

    pos += 4 + ext_len;
  }

  return false;
}

bool append_one_byte_to_ech_extension_value(td::string &wire) {
  auto offsets = get_hello_offsets(wire);
  td::MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    auto ext_end = ext_value_start + ext_len;
    CHECK(ext_end <= offsets.extensions_end);

    if (ext_type == 0xFE0D) {
      wire.insert(ext_end, 1, static_cast<char>(0x00));
      mutable_wire = td::MutableSlice(wire);

      auto new_ext_len = ext_len + 1;
      CHECK(new_ext_len <= 0xFFFFu);
      write_u16(mutable_wire, pos + 2, static_cast<td::uint16>(new_ext_len));

      auto old_extensions_len = static_cast<size_t>(read_u16(mutable_wire, offsets.extensions_length_offset));
      auto new_extensions_len = old_extensions_len + 1;
      CHECK(new_extensions_len <= 0xFFFFu);
      write_u16(mutable_wire, offsets.extensions_length_offset, static_cast<td::uint16>(new_extensions_len));
      update_hello_length_headers(mutable_wire, 1);
      return true;
    }

    pos += 4 + ext_len;
  }

  return false;
}

bool set_extension_type(td::string &wire, td::uint16 from_type, td::uint16 to_type) {
  auto offsets = get_hello_offsets(wire);
  td::MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    CHECK(pos + 4 + ext_len <= offsets.extensions_end);

    if (ext_type == from_type) {
      write_u16(mutable_wire, pos, to_type);
      return true;
    }

    pos += 4 + ext_len;
  }

  return false;
}

bool set_padding_first_byte(td::string &wire, td::uint8 value) {
  auto offsets = get_hello_offsets(wire);
  td::MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    CHECK(ext_value_start + ext_len <= offsets.extensions_end);

    if (ext_type == 0x0015) {
      if (ext_len == 0) {
        return false;
      }
      mutable_wire[ext_value_start] = static_cast<char>(value);
      return true;
    }

    pos += 4 + ext_len;
  }

  return false;
}

TEST(TlsHelloParserSecurity, RejectsUnexpectedRecordAndHandshakeTypes) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string bad_record = wire;
  bad_record[0] = '\x15';
  ASSERT_TRUE(parse_tls_client_hello(bad_record).is_error());

  td::string bad_handshake = wire;
  auto offsets = get_hello_offsets(wire);
  bad_handshake[offsets.handshake_type_offset] = '\x02';
  ASSERT_TRUE(parse_tls_client_hello(bad_handshake).is_error());
}

TEST(TlsHelloParserSecurity, RejectsInvalidCompressionMethods) {
  auto wire = build_ech_enabled_client_hello(1712345678);
  auto offsets = get_hello_offsets(wire);

  td::string tampered = wire;
  tampered[offsets.compression_methods_offset] = '\x01';

  ASSERT_TRUE(parse_tls_client_hello(tampered).is_error());
}

TEST(TlsHelloParserSecurity, RejectsDuplicateCriticalExtensions) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string dup_supported_groups = wire;
  ASSERT_TRUE(duplicate_extension(dup_supported_groups, 0x000A));
  ASSERT_TRUE(parse_tls_client_hello(dup_supported_groups).is_error());

  td::string dup_ech = wire;
  ASSERT_TRUE(duplicate_extension(dup_ech, 0xFE0D));
  ASSERT_TRUE(parse_tls_client_hello(dup_ech).is_error());
}

TEST(TlsHelloParserSecurity, RejectsDuplicateGroupsInsideVectorPayloads) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string dup_supported_group = wire;
  ASSERT_TRUE(duplicate_supported_group(dup_supported_group));
  ASSERT_TRUE(parse_tls_client_hello(dup_supported_group).is_error());

  td::string dup_key_share_group = wire;
  ASSERT_TRUE(duplicate_key_share_group(dup_key_share_group));
  ASSERT_TRUE(parse_tls_client_hello(dup_key_share_group).is_error());
}

TEST(TlsHelloParserSecurity, RejectsMalformedEchSuitePrefixFields) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string bad_outer = wire;
  ASSERT_TRUE(mutate_ech_value_prefix(bad_outer, 0x01, 0x0001, 0x0001));
  ASSERT_TRUE(parse_tls_client_hello(bad_outer).is_error());

  td::string bad_kdf = wire;
  ASSERT_TRUE(mutate_ech_value_prefix(bad_kdf, 0x00, 0x0002, 0x0001));
  ASSERT_TRUE(parse_tls_client_hello(bad_kdf).is_error());

  td::string bad_aead = wire;
  ASSERT_TRUE(mutate_ech_value_prefix(bad_aead, 0x00, 0x0001, 0x0002));
  ASSERT_TRUE(parse_tls_client_hello(bad_aead).is_error());
}

TEST(TlsHelloParserSecurity, RejectsZeroLengthEchEncapsulatedKey) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string bad_enc_len = wire;
  ASSERT_TRUE(set_ech_declared_enc_length(bad_enc_len, 0));
  ASSERT_TRUE(parse_tls_client_hello(bad_enc_len).is_error());
}

TEST(TlsHelloParserSecurity, RejectsZeroLengthEchPayload) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string bad_payload_len = wire;
  ASSERT_TRUE(set_ech_payload_length(bad_payload_len, 0));
  ASSERT_TRUE(parse_tls_client_hello(bad_payload_len).is_error());
}

TEST(TlsHelloParserSecurity, RejectsEchTrailingBytesWhenLengthsAreConsistent) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string bad_trailing = wire;
  ASSERT_TRUE(append_one_byte_to_ech_extension_value(bad_trailing));
  ASSERT_TRUE(parse_tls_client_hello(bad_trailing).is_error());
}

TEST(TlsHelloParserSecurity, RejectsLegacyEchExtensionTypeFe02) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string legacy_ech = wire;
  ASSERT_TRUE(set_extension_type(legacy_ech, 0xFE0D, 0xFE02));
  ASSERT_TRUE(parse_tls_client_hello(legacy_ech).is_error());
}

TEST(TlsHelloParserSecurity, RejectsNonZeroPaddingBytes) {
  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  bool mutated = false;
  for (int i = 0; i < 256; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i, hints);
    td::string non_zero_padding = wire;
    if (!set_padding_first_byte(non_zero_padding, 0x01)) {
      continue;
    }
    mutated = true;
    ASSERT_TRUE(parse_tls_client_hello(non_zero_padding).is_error());
    break;
  }

  ASSERT_TRUE(mutated);
}

}  // namespace
