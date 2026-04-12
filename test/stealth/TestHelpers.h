// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"

#include "td/utils/crypto.h"
#include "td/utils/misc.h"

#include <algorithm>

namespace td {
namespace mtproto {
namespace test {

inline bool is_grease_value(uint16 value) {
  auto hi = static_cast<uint8>((value >> 8) & 0xFF);
  auto lo = static_cast<uint8>(value & 0xFF);
  return hi == lo && (hi & 0x0F) == 0x0A;
}

inline uint16 sample_grease_value(stealth::IRng &rng) {
  char grease = 0;
  rng.fill_secure_bytes(MutableSlice(&grease, 1));
  grease = static_cast<char>((grease & 0xF0) + 0x0A);
  auto grease_byte = static_cast<uint8>(grease);
  return static_cast<uint16>((grease_byte << 8) | grease_byte);
}

inline Result<vector<uint16>> parse_cipher_suite_vector(Slice data) {
  if ((data.size() % 2) != 0) {
    return Status::Error("Cipher suite vector length must be even");
  }

  TlsReader reader(data);
  vector<uint16> cipher_suites;
  cipher_suites.reserve(data.size() / 2);
  while (reader.left() > 0) {
    TRY_RESULT(cipher_suite, reader.read_u16());
    cipher_suites.push_back(cipher_suite);
  }
  return cipher_suites;
}

inline Result<vector<uint8>> parse_ec_point_formats_vector(Slice data) {
  TlsReader reader(data);
  TRY_RESULT(formats_len, reader.read_u8());
  if (reader.left() != formats_len) {
    return Status::Error("ec_point_formats length mismatch");
  }

  vector<uint8> formats;
  formats.reserve(formats_len);
  while (reader.left() > 0) {
    TRY_RESULT(format, reader.read_u8());
    formats.push_back(format);
  }
  return formats;
}

inline vector<uint16> extract_cipher_suites(Slice client_hello) {
  auto parsed = parse_tls_client_hello(client_hello);
  CHECK(parsed.is_ok());
  return parse_cipher_suite_vector(parsed.ok().cipher_suites).move_as_ok();
}

inline vector<uint16> extract_supported_groups(Slice client_hello) {
  auto parsed = parse_tls_client_hello(client_hello);
  CHECK(parsed.is_ok());
  return parsed.ok().supported_groups;
}

inline vector<uint16> extract_key_share_groups(Slice client_hello) {
  auto parsed = parse_tls_client_hello(client_hello);
  CHECK(parsed.is_ok());
  return parsed.ok().key_share_groups;
}

inline bool has_extension(Slice client_hello, uint16 type) {
  auto parsed = parse_tls_client_hello(client_hello);
  CHECK(parsed.is_ok());
  return find_extension(parsed.ok(), type) != nullptr;
}

inline size_t find_extension_position(Slice client_hello, uint16 type) {
  auto parsed = parse_tls_client_hello(client_hello);
  CHECK(parsed.is_ok());
  for (size_t i = 0; i < parsed.ok().extensions.size(); i++) {
    if (parsed.ok().extensions[i].type == type) {
      return i;
    }
  }
  return static_cast<size_t>(-1);
}

inline string extract_extension_body(Slice client_hello, uint16 type) {
  auto parsed = parse_tls_client_hello(client_hello);
  CHECK(parsed.is_ok());
  auto *extension = find_extension(parsed.ok(), type);
  if (extension == nullptr) {
    return string();
  }
  return string(extension->value.data(), extension->value.size());
}

inline string join_uint16_decimal(const vector<uint16> &values) {
  string result;
  for (size_t i = 0; i < values.size(); i++) {
    if (i != 0) {
      result.push_back('-');
    }
    result += to_string(values[i]);
  }
  return result;
}

inline string join_uint8_decimal(const vector<uint8> &values) {
  string result;
  for (size_t i = 0; i < values.size(); i++) {
    if (i != 0) {
      result.push_back('-');
    }
    result += to_string(values[i]);
  }
  return result;
}

inline string canonical_ja3_tuple(Slice client_hello) {
  auto parsed = parse_tls_client_hello(client_hello);
  CHECK(parsed.is_ok());

  auto cipher_suites = parse_cipher_suite_vector(parsed.ok().cipher_suites).move_as_ok();
  cipher_suites.erase(std::remove_if(cipher_suites.begin(), cipher_suites.end(), is_grease_value), cipher_suites.end());

  vector<uint16> extension_types;
  extension_types.reserve(parsed.ok().extensions.size());
  for (const auto &extension : parsed.ok().extensions) {
    if (!is_grease_value(extension.type)) {
      extension_types.push_back(extension.type);
    }
  }

  vector<uint16> supported_groups;
  supported_groups.reserve(parsed.ok().supported_groups.size());
  for (auto group : parsed.ok().supported_groups) {
    if (!is_grease_value(group)) {
      supported_groups.push_back(group);
    }
  }

  vector<uint8> ec_point_formats;
  if (auto *ec_point_formats_extension = find_extension(parsed.ok(), 0x000B)) {
    ec_point_formats = parse_ec_point_formats_vector(ec_point_formats_extension->value).move_as_ok();
  }

  return PSTRING() << parsed.ok().client_legacy_version << ',' << join_uint16_decimal(cipher_suites) << ','
                   << join_uint16_decimal(extension_types) << ',' << join_uint16_decimal(supported_groups) << ','
                   << join_uint8_decimal(ec_point_formats);
}

#if TD_HAVE_OPENSSL
inline string compute_ja3(Slice client_hello) {
  auto tuple = canonical_ja3_tuple(client_hello);
  string digest(16, '\0');
  md5(tuple, digest);
  return hex_encode(digest);
}
#else
inline string compute_ja3(Slice client_hello) {
  UNREACHABLE();
  return client_hello.str();
}
#endif

}  // namespace test
}  // namespace mtproto
}  // namespace td