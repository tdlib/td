//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "test/stealth/FingerprintFixtures.h"

#include "td/utils/BigNum.h"
#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <unordered_set>

namespace td {
namespace mtproto {
namespace test {

struct ParsedExtension final {
  uint16 type{0};
  Slice value;
};

struct ParsedKeyShareEntry final {
  uint16 group{0};
  uint16 key_length{0};
  Slice key_data;
};

struct ParsedClientHello final {
  uint8 record_type{0};
  uint16 record_legacy_version{0};
  uint16 record_length{0};
  uint8 handshake_type{0};
  uint32 handshake_length{0};
  uint16 client_legacy_version{0};
  Slice session_id;
  Slice cipher_suites;
  Slice compression_methods;
  vector<ParsedExtension> extensions;
  vector<uint16> supported_groups;
  vector<uint16> key_share_groups;
  vector<ParsedKeyShareEntry> key_share_entries;
  Slice ech_enc;
  uint16 ech_declared_enc_length{0};
  uint16 ech_actual_enc_length{0};
  uint16 ech_payload_length{0};
};

class TlsReader final {
 public:
  explicit TlsReader(Slice data) : data_(data) {
  }

  Result<uint8> read_u8() {
    if (left() < 1) {
      return Status::Error("Unexpected EOF (u8)");
    }
    return static_cast<uint8>(data_[offset_++]);
  }

  Result<uint16> read_u16() {
    if (left() < 2) {
      return Status::Error("Unexpected EOF (u16)");
    }
    uint16 hi = static_cast<uint8>(data_[offset_]);
    uint16 lo = static_cast<uint8>(data_[offset_ + 1]);
    offset_ += 2;
    return static_cast<uint16>((hi << 8) | lo);
  }

  Result<uint32> read_u24() {
    if (left() < 3) {
      return Status::Error("Unexpected EOF (u24)");
    }
    uint32 b0 = static_cast<uint8>(data_[offset_]);
    uint32 b1 = static_cast<uint8>(data_[offset_ + 1]);
    uint32 b2 = static_cast<uint8>(data_[offset_ + 2]);
    offset_ += 3;
    return (b0 << 16) | (b1 << 8) | b2;
  }

  Result<Slice> read_slice(size_t length) {
    if (left() < length) {
      return Status::Error("Unexpected EOF (slice)");
    }
    auto res = data_.substr(offset_, length);
    offset_ += length;
    return res;
  }

  size_t left() const {
    return data_.size() - offset_;
  }

 private:
  Slice data_;
  size_t offset_{0};
};

inline const ParsedExtension *find_extension(const ParsedClientHello &hello, uint16 type) {
  for (const auto &ext : hello.extensions) {
    if (ext.type == type) {
      return &ext;
    }
  }
  return nullptr;
}

inline bool is_curve25519_quadratic_residue(const BigNum &value) {
  BigNum mod = BigNum::from_hex("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffed").move_as_ok();
  BigNum pow = BigNum::from_hex("3ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff6").move_as_ok();

  BigNumContext context;
  BigNum result;
  BigNum::mod_exp(result, value, pow, mod, context);
  return result.to_decimal() == "1";
}

inline BigNum compute_curve25519_y2(BigNum x) {
  BigNum mod = BigNum::from_hex("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffed").move_as_ok();
  BigNumContext context;
  BigNum result = x.clone();
  BigNum coefficient = BigNum::from_decimal("486662").move_as_ok();
  BigNum one = BigNum::from_decimal("1").move_as_ok();

  BigNum::mod_add(result, result, coefficient, mod, context);
  BigNum::mod_mul(result, result, x, mod, context);
  BigNum::mod_add(result, result, one, mod, context);
  BigNum::mod_mul(result, result, x, mod, context);
  return result;
}

inline bool is_valid_curve25519_public_coordinate(Slice public_key) {
  if (public_key.size() != fixtures::kX25519KeyShareLength) {
    return false;
  }

  auto x = BigNum::from_le_binary(public_key);
  return is_curve25519_quadratic_residue(compute_curve25519_y2(x));
}

inline Result<vector<uint16>> parse_supported_groups(Slice data) {
  TlsReader reader(data);
  TRY_RESULT(groups_len, reader.read_u16());
  if ((groups_len % 2) != 0) {
    return Status::Error("supported_groups vector length must be even");
  }
  if (reader.left() != groups_len) {
    return Status::Error("supported_groups length mismatch");
  }
  vector<uint16> groups;
  std::unordered_set<uint16> unique_groups;
  groups.reserve(groups_len / 2);
  while (reader.left() > 0) {
    TRY_RESULT(group, reader.read_u16());
    if (!unique_groups.insert(group).second) {
      return Status::Error("supported_groups contains duplicate group id");
    }
    groups.push_back(group);
  }
  return groups;
}

inline Result<vector<uint16>> parse_key_share_groups(Slice data, vector<ParsedKeyShareEntry> *entries) {
  TlsReader reader(data);
  TRY_RESULT(shares_len, reader.read_u16());
  if (reader.left() != shares_len) {
    return Status::Error("key_share length mismatch");
  }

  vector<uint16> groups;
  std::unordered_set<uint16> unique_groups;
  while (reader.left() > 0) {
    if (reader.left() < 4) {
      return Status::Error("Truncated key_share entry");
    }
    TRY_RESULT(group, reader.read_u16());
    TRY_RESULT(key_len, reader.read_u16());
    if (key_len == 0) {
      return Status::Error("key_share entry key length must be non-zero");
    }
    switch (group) {
      case fixtures::kX25519Group:
        if (key_len != fixtures::kX25519KeyShareLength) {
          return Status::Error("X25519 key_share length mismatch");
        }
        break;
      case fixtures::kPqHybridGroup:
      case fixtures::kPqHybridDraftGroup:
        if (key_len != fixtures::kPqHybridKeyShareLength) {
          return Status::Error("PQ hybrid key_share length mismatch");
        }
        break;
      default:
        break;
    }
    TRY_RESULT(key_data, reader.read_slice(key_len));
    switch (group) {
      case fixtures::kX25519Group:
        if (!is_valid_curve25519_public_coordinate(key_data)) {
          return Status::Error("X25519 key_share coordinate is invalid");
        }
        break;
      case fixtures::kPqHybridGroup:
      case fixtures::kPqHybridDraftGroup: {
        auto x25519_tail =
            key_data.substr(key_data.size() - fixtures::kX25519KeyShareLength, fixtures::kX25519KeyShareLength);
        if (!is_valid_curve25519_public_coordinate(x25519_tail)) {
          return Status::Error("PQ hybrid key_share X25519 tail is invalid");
        }
        break;
      }
      default:
        break;
    }
    if (entries != nullptr) {
      entries->push_back(ParsedKeyShareEntry{group, key_len, key_data});
    }
    if (!unique_groups.insert(group).second) {
      return Status::Error("key_share contains duplicate group id");
    }
    groups.push_back(group);
  }
  return groups;
}

inline Result<ParsedClientHello> parse_tls_client_hello(Slice wire) {
  ParsedClientHello res;
  TlsReader reader(wire);

  TRY_RESULT(record_type, reader.read_u8());
  res.record_type = record_type;
  if (res.record_type != 0x16) {
    return Status::Error("Unexpected TLS record type");
  }
  TRY_RESULT(record_legacy_version, reader.read_u16());
  res.record_legacy_version = record_legacy_version;
  if (res.record_legacy_version != 0x0301 && res.record_legacy_version != 0x0303) {
    return Status::Error("Unexpected TLS record legacy version");
  }
  TRY_RESULT(record_length, reader.read_u16());
  res.record_length = record_length;
  if (reader.left() != res.record_length) {
    return Status::Error("TLS record length mismatch");
  }

  TRY_RESULT(handshake_type, reader.read_u8());
  res.handshake_type = handshake_type;
  if (res.handshake_type != 0x01) {
    return Status::Error("Unexpected TLS handshake type");
  }
  TRY_RESULT(handshake_length, reader.read_u24());
  res.handshake_length = handshake_length;
  if (reader.left() != res.handshake_length) {
    return Status::Error("Handshake length mismatch");
  }

  TRY_RESULT(client_legacy_version, reader.read_u16());
  res.client_legacy_version = client_legacy_version;
  if (res.client_legacy_version != 0x0303) {
    return Status::Error("Unexpected ClientHello legacy version");
  }
  TRY_RESULT(ignore_random, reader.read_slice(32));
  (void)ignore_random;

  TRY_RESULT(session_id_len, reader.read_u8());
  TRY_RESULT(session_id, reader.read_slice(session_id_len));
  res.session_id = session_id;

  TRY_RESULT(cipher_suites_len, reader.read_u16());
  if ((cipher_suites_len % 2) != 0) {
    return Status::Error("Cipher suites length must be even");
  }
  TRY_RESULT(cipher_suites, reader.read_slice(cipher_suites_len));
  res.cipher_suites = cipher_suites;

  TRY_RESULT(compression_methods_len, reader.read_u8());
  TRY_RESULT(compression_methods, reader.read_slice(compression_methods_len));
  res.compression_methods = compression_methods;
  if (res.compression_methods.size() != 1 || static_cast<uint8>(res.compression_methods[0]) != 0) {
    return Status::Error("Unexpected compression methods for TLS 1.3 ClientHello");
  }

  TRY_RESULT(extensions_len, reader.read_u16());
  TRY_RESULT(extensions_raw, reader.read_slice(extensions_len));
  if (reader.left() != 0) {
    return Status::Error("Trailing bytes after ClientHello");
  }

  TlsReader ext_reader(extensions_raw);
  std::unordered_set<uint16> extension_types;
  while (ext_reader.left() > 0) {
    ParsedExtension ext;
    TRY_RESULT(ext_type, ext_reader.read_u16());
    ext.type = ext_type;
    if (ext.type == 0xFE02) {
      return Status::Error("Legacy ECH extension type 0xFE02 is forbidden");
    }
    if (!extension_types.insert(ext.type).second) {
      return Status::Error("Duplicate extension type in ClientHello");
    }
    TRY_RESULT(ext_len, ext_reader.read_u16());
    TRY_RESULT(ext_value, ext_reader.read_slice(ext_len));
    if (ext.type == 0x0015) {
      for (auto c : ext_value) {
        if (static_cast<uint8>(c) != 0) {
          return Status::Error("padding extension must contain only zero bytes");
        }
      }
    }
    ext.value = ext_value;
    res.extensions.push_back(ext);
  }

  if (auto *supported_groups = find_extension(res, 0x000A)) {
    TRY_RESULT(parsed_supported_groups, parse_supported_groups(supported_groups->value));
    res.supported_groups = std::move(parsed_supported_groups);
  }
  if (auto *key_share = find_extension(res, 0x0033)) {
    TRY_RESULT(parsed_key_share_groups, parse_key_share_groups(key_share->value, &res.key_share_entries));
    res.key_share_groups = std::move(parsed_key_share_groups);
  }

  if (!res.key_share_groups.empty()) {
    if (res.supported_groups.empty()) {
      return Status::Error("key_share is present without supported_groups");
    }
    std::unordered_set<uint16> supported_groups(res.supported_groups.begin(), res.supported_groups.end());
    for (auto group : res.key_share_groups) {
      if (supported_groups.count(group) == 0) {
        return Status::Error("key_share contains a group that is absent in supported_groups");
      }
    }
  }

  if (auto *ech = find_extension(res, 0xFE0D)) {
    if (ech->value.size() < 10) {
      return Status::Error("ECH extension too short");
    }

    TlsReader ech_reader(ech->value);
    TRY_RESULT(ech_outer_client_hello, ech_reader.read_u8());
    if (ech_outer_client_hello != 0x00) {
      return Status::Error("ECH outer ClientHello marker must be 0x00");
    }
    TRY_RESULT(ech_kdf_id, ech_reader.read_u16());
    if (ech_kdf_id != 0x0001) {
      return Status::Error("Unexpected ECH KDF identifier");
    }
    TRY_RESULT(ech_aead_id, ech_reader.read_u16());
    if (ech_aead_id != 0x0001) {
      return Status::Error("Unexpected ECH AEAD identifier");
    }
    TRY_RESULT(ignore_config_id, ech_reader.read_u8());
    (void)ignore_config_id;

    TRY_RESULT(ech_declared_enc_length, ech_reader.read_u16());
    if (ech_declared_enc_length == 0) {
      return Status::Error("ECH encapsulated key length must be non-zero");
    }
    res.ech_declared_enc_length = ech_declared_enc_length;
    TRY_RESULT(ech_enc, ech_reader.read_slice(res.ech_declared_enc_length));
    if (res.ech_declared_enc_length == fixtures::kX25519KeyShareLength &&
        !is_valid_curve25519_public_coordinate(ech_enc)) {
      return Status::Error("ECH encapsulated key coordinate is invalid");
    }
    res.ech_enc = ech_enc;
    res.ech_actual_enc_length = static_cast<uint16>(ech_enc.size());

    TRY_RESULT(ech_payload_length, ech_reader.read_u16());
    if (ech_payload_length == 0) {
      return Status::Error("ECH payload length must be non-zero");
    }
    res.ech_payload_length = ech_payload_length;
    TRY_RESULT(ignore_ech_payload, ech_reader.read_slice(res.ech_payload_length));
    (void)ignore_ech_payload;

    if (ech_reader.left() != 0) {
      return Status::Error("ECH extension trailing bytes");
    }
  }

  return res;
}

}  // namespace test
}  // namespace mtproto
}  // namespace td
