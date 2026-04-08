// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "test/stealth/TlsHelloParsers.h"

namespace td {
namespace mtproto {
namespace test {

struct HelloOffsets final {
  size_t handshake_type_offset{0};
  size_t compression_methods_offset{0};
  size_t extensions_length_offset{0};
  size_t extensions_start{0};
  size_t extensions_end{0};
};

inline uint16 read_u16(Slice data, size_t offset) {
  CHECK(offset + 2 <= data.size());
  return static_cast<uint16>((static_cast<uint8>(data[offset]) << 8) | static_cast<uint8>(data[offset + 1]));
}

inline uint32 read_u24(Slice data, size_t offset) {
  CHECK(offset + 3 <= data.size());
  return (static_cast<uint32>(static_cast<uint8>(data[offset])) << 16) |
         (static_cast<uint32>(static_cast<uint8>(data[offset + 1])) << 8) |
         static_cast<uint32>(static_cast<uint8>(data[offset + 2]));
}

inline void write_u16(MutableSlice data, size_t offset, uint16 value) {
  CHECK(offset + 2 <= data.size());
  data[offset] = static_cast<char>((value >> 8) & 0xff);
  data[offset + 1] = static_cast<char>(value & 0xff);
}

inline void write_u24(MutableSlice data, size_t offset, uint32 value) {
  CHECK(offset + 3 <= data.size());
  data[offset] = static_cast<char>((value >> 16) & 0xff);
  data[offset + 1] = static_cast<char>((value >> 8) & 0xff);
  data[offset + 2] = static_cast<char>(value & 0xff);
}

inline HelloOffsets get_hello_offsets(Slice wire) {
  CHECK(wire.size() >= 48);

  HelloOffsets offsets;
  offsets.handshake_type_offset = 5;

  size_t pos = 9;
  pos += 2;
  pos += 32;

  CHECK(pos < wire.size());
  auto session_id_len = static_cast<size_t>(static_cast<uint8>(wire[pos]));
  pos += 1 + session_id_len;

  auto cipher_suites_len = static_cast<size_t>(read_u16(wire, pos));
  pos += 2 + cipher_suites_len;

  CHECK(pos < wire.size());
  auto compression_methods_len = static_cast<size_t>(static_cast<uint8>(wire[pos]));
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

inline void update_hello_length_headers(MutableSlice wire, size_t added_bytes) {
  auto record_len = static_cast<size_t>(read_u16(wire, 3));
  auto handshake_len = static_cast<size_t>(read_u24(wire, 6));

  record_len += added_bytes;
  handshake_len += added_bytes;

  CHECK(record_len <= 0xFFFFu);
  CHECK(handshake_len <= 0xFFFFFFu);

  write_u16(wire, 3, static_cast<uint16>(record_len));
  write_u24(wire, 6, static_cast<uint32>(handshake_len));
}

inline bool duplicate_extension(string &wire, uint16 ext_type) {
  auto offsets = get_hello_offsets(wire);
  Slice view(wire);

  size_t pos = offsets.extensions_start;
  string ext_blob;
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

  auto mutable_wire = MutableSlice(wire);
  auto old_extensions_len = static_cast<size_t>(read_u16(mutable_wire, offsets.extensions_length_offset));
  auto new_extensions_len = old_extensions_len + ext_blob.size();
  CHECK(new_extensions_len <= 0xFFFFu);
  write_u16(mutable_wire, offsets.extensions_length_offset, static_cast<uint16>(new_extensions_len));
  update_hello_length_headers(mutable_wire, ext_blob.size());
  return true;
}

inline bool duplicate_supported_group(string &wire) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

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

inline bool duplicate_key_share_group(string &wire) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

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

inline bool mutate_ech_value_prefix(string &wire, uint8 outer, uint16 kdf_id, uint16 aead_id) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

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
      mutable_wire[ext_value_start] = static_cast<char>(outer);
      write_u16(mutable_wire, ext_value_start + 1, kdf_id);
      write_u16(mutable_wire, ext_value_start + 3, aead_id);
      return true;
    }

    pos += 4 + ext_len;
  }

  return false;
}

inline bool set_ech_declared_enc_length(string &wire, uint16 enc_length) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

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

inline bool set_ech_payload_length(string &wire, uint16 payload_length) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

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

inline bool append_one_byte_to_ech_extension_value(string &wire) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

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
      mutable_wire = MutableSlice(wire);

      auto new_ext_len = ext_len + 1;
      CHECK(new_ext_len <= 0xFFFFu);
      write_u16(mutable_wire, pos + 2, static_cast<uint16>(new_ext_len));

      auto old_extensions_len = static_cast<size_t>(read_u16(mutable_wire, offsets.extensions_length_offset));
      auto new_extensions_len = old_extensions_len + 1;
      CHECK(new_extensions_len <= 0xFFFFu);
      write_u16(mutable_wire, offsets.extensions_length_offset, static_cast<uint16>(new_extensions_len));
      update_hello_length_headers(mutable_wire, 1);
      return true;
    }

    pos += 4 + ext_len;
  }

  return false;
}

inline bool set_extension_type(string &wire, uint16 from_type, uint16 to_type) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

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

inline bool set_padding_first_byte(string &wire, uint8 value) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

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

inline bool set_key_share_entry_length(string &wire, uint16 group, uint16 key_length) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    CHECK(ext_value_start + ext_len <= offsets.extensions_end);

    if (ext_type == 0x0033) {
      auto shares_len = static_cast<size_t>(read_u16(mutable_wire, ext_value_start));
      size_t share_pos = ext_value_start + 2;
      size_t share_end = share_pos + shares_len;
      if (share_end > ext_value_start + ext_len) {
        return false;
      }

      while (share_pos < share_end) {
        if (share_pos + 4 > share_end) {
          return false;
        }
        auto current_group = read_u16(mutable_wire, share_pos);
        auto current_key_len = static_cast<size_t>(read_u16(mutable_wire, share_pos + 2));
        auto next_share = share_pos + 4 + current_key_len;
        if (next_share > share_end) {
          return false;
        }

        if (current_group == group) {
          write_u16(mutable_wire, share_pos + 2, key_length);
          return true;
        }
        share_pos = next_share;
      }
      return false;
    }

    pos += 4 + ext_len;
  }

  return false;
}

inline bool set_supported_group_value(string &wire, uint16 from_group, uint16 to_group) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    CHECK(ext_value_start + ext_len <= offsets.extensions_end);

    if (ext_type == 0x000A) {
      if (ext_len < 4) {
        return false;
      }
      auto groups_len = static_cast<size_t>(read_u16(mutable_wire, ext_value_start));
      size_t group_pos = ext_value_start + 2;
      size_t group_end = group_pos + groups_len;
      if (group_end > ext_value_start + ext_len) {
        return false;
      }

      while (group_pos < group_end) {
        auto current_group = read_u16(mutable_wire, group_pos);
        if (current_group == from_group) {
          write_u16(mutable_wire, group_pos, to_group);
          return true;
        }
        group_pos += 2;
      }
      return false;
    }

    pos += 4 + ext_len;
  }

  return false;
}

inline bool set_key_share_entry_group(string &wire, uint16 from_group, uint16 to_group) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    CHECK(ext_value_start + ext_len <= offsets.extensions_end);

    if (ext_type == 0x0033) {
      auto shares_len = static_cast<size_t>(read_u16(mutable_wire, ext_value_start));
      size_t share_pos = ext_value_start + 2;
      size_t share_end = share_pos + shares_len;
      if (share_end > ext_value_start + ext_len) {
        return false;
      }

      while (share_pos < share_end) {
        if (share_pos + 4 > share_end) {
          return false;
        }
        auto current_group = read_u16(mutable_wire, share_pos);
        auto current_key_len = static_cast<size_t>(read_u16(mutable_wire, share_pos + 2));
        auto next_share = share_pos + 4 + current_key_len;
        if (next_share > share_end) {
          return false;
        }

        if (current_group == from_group) {
          write_u16(mutable_wire, share_pos, to_group);
          return true;
        }
        share_pos = next_share;
      }
      return false;
    }

    pos += 4 + ext_len;
  }

  return false;
}

inline bool make_invalid_curve25519_coordinate(MutableSlice key_data) {
  CHECK(key_data.size() == fixtures::kX25519KeyShareLength);

  string candidate = key_data.str();
  for (size_t byte_index = 0; byte_index < candidate.size(); byte_index++) {
    for (uint8 bit = 1; bit != 0; bit <<= 1) {
      auto mutated = candidate;
      mutated[byte_index] = static_cast<char>(static_cast<uint8>(mutated[byte_index]) ^ bit);
      if (!is_valid_curve25519_public_coordinate(mutated)) {
        key_data.copy_from(mutated);
        return true;
      }
    }
  }
  return false;
}

inline bool corrupt_key_share_coordinate(string &wire, uint16 group, bool mutate_tail_only) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    CHECK(ext_value_start + ext_len <= offsets.extensions_end);

    if (ext_type == 0x0033) {
      auto shares_len = static_cast<size_t>(read_u16(mutable_wire, ext_value_start));
      size_t share_pos = ext_value_start + 2;
      size_t share_end = share_pos + shares_len;
      if (share_end > ext_value_start + ext_len) {
        return false;
      }

      while (share_pos < share_end) {
        if (share_pos + 4 > share_end) {
          return false;
        }
        auto current_group = read_u16(mutable_wire, share_pos);
        auto current_key_len = static_cast<size_t>(read_u16(mutable_wire, share_pos + 2));
        auto key_data_offset = share_pos + 4;
        auto next_share = key_data_offset + current_key_len;
        if (next_share > share_end) {
          return false;
        }

        if (current_group == group) {
          if (mutate_tail_only) {
            if (current_key_len < fixtures::kX25519KeyShareLength) {
              return false;
            }
            auto tail_offset = next_share - fixtures::kX25519KeyShareLength;
            auto tail = mutable_wire.substr(tail_offset, fixtures::kX25519KeyShareLength);
            return make_invalid_curve25519_coordinate(tail);
          }

          if (current_key_len != fixtures::kX25519KeyShareLength) {
            return false;
          }
          auto key_data = mutable_wire.substr(key_data_offset, current_key_len);
          return make_invalid_curve25519_coordinate(key_data);
        }
        share_pos = next_share;
      }
      return false;
    }

    pos += 4 + ext_len;
  }

  return false;
}

inline bool corrupt_ech_enc_coordinate(string &wire) {
  auto offsets = get_hello_offsets(wire);
  MutableSlice mutable_wire(wire);

  size_t pos = offsets.extensions_start;
  while (pos < offsets.extensions_end) {
    CHECK(pos + 4 <= offsets.extensions_end);
    auto ext_type = read_u16(mutable_wire, pos);
    auto ext_len = static_cast<size_t>(read_u16(mutable_wire, pos + 2));
    auto ext_value_start = pos + 4;
    auto ext_end = ext_value_start + ext_len;
    CHECK(ext_end <= offsets.extensions_end);

    if (ext_type == 0xFE0D) {
      if (ext_len < 8) {
        return false;
      }
      auto enc_len = static_cast<size_t>(read_u16(mutable_wire, ext_value_start + 6));
      if (enc_len != fixtures::kX25519KeyShareLength) {
        return false;
      }
      auto enc_offset = ext_value_start + 8;
      if (enc_offset + enc_len > ext_end) {
        return false;
      }
      auto enc = mutable_wire.substr(enc_offset, enc_len);
      return make_invalid_curve25519_coordinate(enc);
    }

    pos += 4 + ext_len;
  }

  return false;
}

}  // namespace test
}  // namespace mtproto
}  // namespace td