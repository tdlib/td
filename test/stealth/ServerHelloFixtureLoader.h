// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

// Loads reviewed ServerHello JSON artifacts at test time and synthesizes
// a minimal TLS 1.3 ServerHello wire from the structured fields, so the
// in-tree `parse_tls_server_hello` can be exercised end-to-end without
// embedding wire_hex bytes in the repository.
//
// The JSON artifacts under `test/analysis/fixtures/serverhello/**` are
// produced by `test/analysis/generate_server_hello_fixture_corpus.py`
// from reviewed pcap samples. Each file carries per-sample fields:
//   - cipher_suite    (hex string, e.g. "0x1301")
//   - selected_version (hex string, e.g. "0x0304")
//   - extensions      (array of hex extension-type strings)
//
// We fold those into a well-formed TLS 1.3 ServerHello record so the
// parser contract tests have a deterministic, repo-local input that
// tracks the reviewed ground truth.

#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/JsonBuilder.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/common.h"
#include "td/utils/filesystem.h"
#include "td/utils/misc.h"

#ifndef TELEMT_TEST_REPO_ROOT
#define TELEMT_TEST_REPO_ROOT ""
#endif

namespace td {
namespace mtproto {
namespace test {

struct ServerHelloFixtureSample final {
  string family;
  uint16 cipher_suite{0};
  uint16 selected_version{0};
  vector<uint16> extension_types;
  string source_path;
};

inline string server_hello_fixture_root() {
  return string(TELEMT_TEST_REPO_ROOT) + "/test/analysis/fixtures/serverhello";
}

inline uint16 parse_hex_u16(Slice hex) {
  string s = hex.str();
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s = s.substr(2);
  }
  uint16 value = 0;
  for (char c : s) {
    int digit = -1;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = 10 + (c - 'a');
    } else if (c >= 'A' && c <= 'F') {
      digit = 10 + (c - 'A');
    } else {
      continue;
    }
    value = static_cast<uint16>((value << 4) | static_cast<uint16>(digit));
  }
  return value;
}

inline Result<ServerHelloFixtureSample> load_server_hello_fixture(CSlice absolute_path) {
  TRY_RESULT(buffer, read_file_str(absolute_path));
  TRY_RESULT(root, json_decode(MutableSlice(buffer)));
  if (root.type() != JsonValue::Type::Object) {
    return Status::Error("ServerHello fixture root is not an object");
  }
  auto &obj = root.get_object();
  ServerHelloFixtureSample sample;
  auto r_family = obj.get_optional_string_field("family");
  if (r_family.is_ok()) {
    sample.family = r_family.move_as_ok();
  }
  auto r_source = obj.get_optional_string_field("source_path");
  if (r_source.is_ok()) {
    sample.source_path = r_source.move_as_ok();
  }

  auto samples_field = obj.extract_required_field("samples", JsonValue::Type::Array);
  if (samples_field.is_error()) {
    return samples_field.move_as_error();
  }
  auto samples_array = samples_field.move_as_ok();
  auto &samples = samples_array.get_array();
  if (samples.empty()) {
    return Status::Error("ServerHello fixture has no samples");
  }
  auto &first = samples[0];
  if (first.type() != JsonValue::Type::Object) {
    return Status::Error("ServerHello fixture sample is not an object");
  }
  auto &first_obj = first.get_object();
  auto r_cipher = first_obj.get_optional_string_field("cipher_suite");
  if (r_cipher.is_error()) {
    return r_cipher.move_as_error();
  }
  sample.cipher_suite = parse_hex_u16(r_cipher.ok());

  auto r_selected_version = first_obj.get_optional_string_field("selected_version");
  if (r_selected_version.is_ok()) {
    sample.selected_version = parse_hex_u16(r_selected_version.ok());
  }

  auto ext_field = first_obj.extract_optional_field("extensions", JsonValue::Type::Array);
  if (ext_field.is_ok()) {
    auto ext_val = ext_field.move_as_ok();
    if (ext_val.type() == JsonValue::Type::Array) {
      for (auto &entry : ext_val.get_array()) {
        if (entry.type() == JsonValue::Type::String) {
          sample.extension_types.push_back(parse_hex_u16(entry.get_string()));
        }
      }
    }
  }

  return sample;
}

inline void append_u8(string &out, uint8 value) {
  out.push_back(static_cast<char>(value));
}

inline void append_u16(string &out, uint16 value) {
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>(value & 0xFF));
}

inline void append_u24(string &out, uint32 value) {
  out.push_back(static_cast<char>((value >> 16) & 0xFF));
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>(value & 0xFF));
}

inline string synthesize_server_hello_wire(const ServerHelloFixtureSample &sample) {
  // Build a minimal TLS 1.3 ServerHello record that carries the
  // fixture's selected cipher_suite and (if announced) the
  // supported_versions extension for the selected TLS version.
  // A key_share with a single X25519 group (0x001D) and a dummy
  // 32-byte public key is always included — this makes the output
  // valid enough to trigger the full parser happy path.

  // Body = server_legacy_version(2) + random(32) + session_id_len(1) +
  //        cipher_suite(2) + compression_method(1) + extensions
  string body;
  append_u16(body, 0x0303);  // server_legacy_version

  // Random: deterministic, fixed bytes (not HRR sentinel).
  for (int i = 0; i < 32; ++i) {
    append_u8(body, static_cast<uint8>(0x10 + (i & 0x0F)));
  }

  append_u8(body, 0);  // empty session_id

  append_u16(body, sample.cipher_suite);
  append_u8(body, 0x00);  // compression_method = null

  // Extensions
  string extensions;

  // supported_versions (0x002B) with selected_version (default 0x0304)
  uint16 selected_version = sample.selected_version != 0 ? sample.selected_version : 0x0304;
  append_u16(extensions, 0x002B);
  append_u16(extensions, 2);
  append_u16(extensions, selected_version);

  // key_share (0x0033): a single X25519 entry (0x001D) with a dummy 32-byte key.
  append_u16(extensions, 0x0033);
  append_u16(extensions, 36);  // group(2) + key_len(2) + key(32)
  append_u16(extensions, 0x001D);
  append_u16(extensions, 32);
  for (int i = 0; i < 32; ++i) {
    append_u8(extensions, static_cast<uint8>(0x20 + (i & 0x0F)));
  }

  append_u16(body, static_cast<uint16>(extensions.size()));
  body.append(extensions);

  // Handshake header
  string handshake;
  append_u8(handshake, 0x02);  // handshake_type = ServerHello
  append_u24(handshake, static_cast<uint32>(body.size()));
  handshake.append(body);

  // Record header
  string wire;
  append_u8(wire, 0x16);        // record_type = handshake
  append_u16(wire, 0x0303);     // record_legacy_version
  append_u16(wire, static_cast<uint16>(handshake.size()));
  wire.append(handshake);

  return wire;
}

// Returns a curated map from BrowserProfile to a representative reviewed
// ServerHello fixture (relative to the serverhello root). These paths are
// hand-picked so every profile resolves to a real reviewed capture; the
// helper is used by the handshake-pairing tests to tie the CH builder
// output to a matching SH.
inline Slice representative_server_hello_path_for_family(Slice family_hint) {
  string lower = to_lower(family_hint);
  if (lower.find("chrome133") != string::npos || lower.find("chrome131") != string::npos ||
      lower.find("chrome120") != string::npos) {
    return Slice("android/chrome143_0_7499_192_android15_1_2_bf770816.serverhello.json");
  }
  if (lower.find("firefox148") != string::npos) {
    return Slice("linux_desktop/firefox148_linux_desktop.serverhello.json");
  }
  if (lower.find("firefox149") != string::npos || lower.find("firefox_macos") != string::npos) {
    return Slice("macos/firefox149_macos26_3.serverhello.json");
  }
  if (lower.find("safari") != string::npos) {
    return Slice("ios/safari26_3_ios26_3_1_83afd3bc.serverhello.json");
  }
  if (lower.find("ios") != string::npos) {
    return Slice("ios/chrome147_0_7727_47_ios26_3.serverhello.json");
  }
  if (lower.find("android") != string::npos) {
    return Slice("android/chrome146_177_android16.serverhello.json");
  }
  // Default fallback: chrome linux desktop.
  return Slice("linux_desktop/chrome144_linux_desktop.serverhello.json");
}

// Loads a fixture given a path relative to the serverhello root
// (e.g. "android/chrome143_0_7499_192_android15_1_2_bf770816.serverhello.json").
inline Result<ServerHelloFixtureSample> load_server_hello_fixture_relative(CSlice relative_path) {
  string full = server_hello_fixture_root() + "/" + relative_path.str();
  return load_server_hello_fixture(full);
}

}  // namespace test
}  // namespace mtproto
}  // namespace td
