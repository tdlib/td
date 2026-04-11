// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/ClientHelloOpMapper.h"

#include "td/utils/common.h"

namespace td {
namespace mtproto {

namespace {

using Op = ClientHelloOp;

string store_u16(uint16 value) {
  string result(2, '\0');
  result[0] = static_cast<char>((value >> 8) & 0xff);
  result[1] = static_cast<char>(value & 0xff);
  return result;
}

string store_u8(uint8 value) {
  return string(1, static_cast<char>(value));
}

uint8 to_uint8(size_t value) {
  CHECK(value <= 255);
  return static_cast<uint8>(value);
}

uint16 to_uint16(size_t value) {
  CHECK(value <= 65535);
  return static_cast<uint16>(value);
}

string encode_u16_list(const vector<uint16> &values) {
  string result;
  result.reserve(values.size() * 2);
  for (auto value : values) {
    result += store_u16(value);
  }
  return result;
}

string encode_u8_list(const vector<uint8> &values) {
  string result;
  result.reserve(values.size());
  for (auto value : values) {
    result.push_back(static_cast<char>(value));
  }
  return result;
}

string encode_str_list(const vector<string> &values) {
  string result;
  for (auto &value : values) {
    CHECK(value.size() < 256);
    result += store_u8(to_uint8(value.size()));
    result += value;
  }
  return result;
}

uint16 get_extension_code(const BrowserExtension &extension) {
  return extension.type == TlsExtensionType::Custom ? extension.custom_type : static_cast<uint16>(extension.type);
}

vector<Op> make_extension_ops(const BrowserProfileSpec &profile, const BrowserExtension &extension,
                              const ExecutorConfig &config) {
  vector<Op> ops;
  auto type = get_extension_code(extension);

  switch (extension.type) {
    case TlsExtensionType::ServerName:
      return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::scope16_begin(), Op::bytes("\x00"),
              Op::scope16_begin(), Op::domain(), Op::scope16_end(), Op::scope16_end(), Op::scope16_end()};
    case TlsExtensionType::SupportedGroups: {
      // The supported_groups list carries the same PQ codepoint that the
      // matching key_share entry advertises (Chrome 131+ uses 0x11EC =
      // X25519MLKEM768; the legacy Chrome 120 PQ snapshot used 0x6399 =
      // X25519Kyber768Draft00). Substitute any occurrence of the canonical
      // 0x11EC with `config.pq_group_id_override` so that test lanes which
      // exercise the legacy snapshot can drive both extensions consistently
      // through the same single override field. Production callers leave
      // the override at the 0x11EC default and the substitution is a no-op.
      vector<uint16> groups = extension.u16_list;
      if (config.pq_group_id_override != 0x11EC) {
        for (auto &g : groups) {
          if (g == 0x11EC) {
            g = config.pq_group_id_override;
          }
        }
      }
      ops = {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::scope16_begin()};
      if (extension.prepend_grease) {
        ops.push_back(Op::grease(4));
      }
      ops.push_back(Op::bytes(encode_u16_list(groups)));
      ops.push_back(Op::scope16_end());
      ops.push_back(Op::scope16_end());
      return ops;
    }
    case TlsExtensionType::EcPointFormats:
      return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::bytes(store_u8(to_uint8(extension.u8_list.size()))),
              Op::bytes(encode_u8_list(extension.u8_list)), Op::scope16_end()};
    case TlsExtensionType::SignatureAlgorithms:
    case TlsExtensionType::DelegatedCredentials:
      return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::scope16_begin(), Op::bytes(encode_u16_list(extension.u16_list)),
              Op::scope16_end(), Op::scope16_end()};
    case TlsExtensionType::Alpn: {
      // Proxy path strips `h2` from the ALPN list and advertises
      // `http/1.1` only. The post-handshake bytes are raw MTProto and
      // any HTTP/2 framing claim would be a self-evident L7 mismatch
      // for any DPI box that parses ALPN. Caller sets
      // `config.force_http11_only_alpn = true` via the proxy builder
      // entry points; production browser-direct path leaves it at the
      // default and emits the profile's full ALPN list verbatim.
      vector<string> alpn = extension.str_list;
      if (config.force_http11_only_alpn) {
        alpn.clear();
        alpn.emplace_back("http/1.1");
      }
      return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::scope16_begin(), Op::bytes(encode_str_list(alpn)),
              Op::scope16_end(), Op::scope16_end()};
    }
    case TlsExtensionType::SupportedVersions:
      ops = {Op::bytes(store_u16(type)), Op::scope16_begin()};
      ops.push_back(Op::bytes(store_u8(to_uint8(extension.u16_list.size() * 2 + (extension.prepend_grease ? 2 : 0)))));
      if (extension.prepend_grease) {
        ops.push_back(Op::grease(6));
      }
      ops.push_back(Op::bytes(encode_u16_list(extension.u16_list)));
      ops.push_back(Op::scope16_end());
      return ops;
    case TlsExtensionType::PskKeyExchangeModes:
      return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::bytes(store_u8(to_uint8(extension.u8_list.size()))),
              Op::bytes(encode_u8_list(extension.u8_list)), Op::scope16_end()};
    case TlsExtensionType::KeyShare:
      if (!extension.raw_data.empty()) {
        return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::bytes(extension.raw_data), Op::scope16_end()};
      }
      ops = {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::scope16_begin()};
      for (auto &entry : extension.key_share_entries) {
        switch (entry.kind) {
          case KeyShareKind::Grease:
            ops.push_back(Op::grease_key_share_entry(entry.grease_index));
            break;
          case KeyShareKind::X25519MlKem768:
            ops.push_back(Op::x25519_ml_kem_768_key_share_entry());
            break;
          case KeyShareKind::X25519:
            ops.push_back(Op::x25519_key_share_entry());
            break;
          case KeyShareKind::Secp256r1:
            ops.push_back(Op::secp256r1_key_share_entry());
            break;
          default:
            UNREACHABLE();
        }
      }
      ops.push_back(Op::scope16_end());
      ops.push_back(Op::scope16_end());
      return ops;
    case TlsExtensionType::PreSharedKey: {
      // TLS 1.3 OfferedPsks structure as observed in real macOS Firefox 149
      // captures (test/analysis/fixtures/clienthello/macos/
      // firefox149_macos26_3.clienthello.json):
      //   2  identities_len = 0x006F
      //   2    identity_len  = 0x0069
      //   105  identity      = random bytes (executor RNG)
      //   4    obfuscated_ticket_age = random bytes
      //   2  binders_len = 0x0021
      //   1    binder_len = 0x20
      //   32   binder      = random bytes
      //
      // Total body = 148 bytes; total extension = type(2) + body_len(2) + 148.
      // Tests: `test/stealth/test_firefox_macos_psk_extension_invariants.cpp`.
      return {Op::bytes(store_u16(type)),
              Op::scope16_begin(),
              Op::bytes("\x00\x6f\x00\x69"),
              Op::random_bytes(105),
              Op::random_bytes(4),
              Op::bytes("\x00\x21\x20"),
              Op::random_bytes(32),
              Op::scope16_end()};
    }
    case TlsExtensionType::EncryptedClientHello:
      if (config.has_ech) {
        // The ECH HPKE encapsulated key field MUST be a valid X25519
        // public point — Cloudflare/Google ECH-aware servers and any
        // DPI middlebox that ships an X25519 validator will reject
        // raw random bytes here. We always emit X25519 (32-byte point)
        // because that is the only HPKE KEM Chrome/Firefox use today;
        // for any other declared length we fall back to opaque bytes
        // so the wire still matches the declared length field.
        CHECK(config.ech_enc_key_length > 0);
        ClientHelloOp enc_key_op = (config.ech_enc_key_length == 32)
                                       ? Op::x25519_public_key()
                                       : Op::random_bytes(config.ech_enc_key_length);
        return {Op::bytes(store_u16(type)),
                Op::scope16_begin(),
                Op::bytes("\x00\x00\x01\x00\x01"),
                Op::random_bytes(1),
                Op::bytes(store_u16(static_cast<uint16>(config.ech_enc_key_length))),
                std::move(enc_key_op),
                Op::scope16_begin(),
                Op::random_bytes(config.ech_payload_length),
                Op::scope16_end(),
                Op::scope16_end()};
      }
      return {};
    case TlsExtensionType::ApplicationSettings: {
      // The ALPS extension uses two different IANA codepoints across
      // Chrome history:
      //   * 0x4469 — Chrome 106..132 (legacy ALPS, used by Chrome 120 / 131)
      //   * 0x44CD — Chrome 133+    (current ALPS, used by Chrome 133)
      //
      // The wire byte for the extension TYPE itself MUST be the
      // profile-specific value, NOT the placeholder enum constant
      // `TlsExtensionType::ApplicationSettings = 17613` (which happens to
      // equal 0x44CD but is just a marker for the switch dispatch).
      //
      // The previous form of this case wrote `store_u16(type)` (always
      // 0x44CD) as the ext header AND `store_u16(config.alps_type)` again
      // inside the body. That produced a wire that:
      //   1. Always advertised 0x44CD regardless of the requested profile,
      //      breaking Chrome 120 / 131 imitation.
      //   2. Carried 2 extra `\x44\x69` (or `\x44\xCD`) bytes inside the
      //      extension body, making the body length 7 instead of 5 — a
      //      malformed ApplicationSettings record that any strict
      //      BoringSSL/Chromium parser rejects.
      //
      // The architecturally correct emit puts `config.alps_type` in the
      // ext header and the unmodified `raw_data` (list_len + protocol)
      // inside the body. Behaviour is enforced by
      // `test/stealth/test_alps_extension_wire_type_invariants.cpp`.
      uint16 wire_type = config.alps_type != 0 ? config.alps_type : type;
      return {Op::bytes(store_u16(wire_type)), Op::scope16_begin(), Op::bytes(extension.raw_data),
              Op::scope16_end()};
    }
    default:
      return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::bytes(extension.raw_data), Op::scope16_end()};
  }
}

vector<vector<Op>> make_extension_parts(const BrowserProfileSpec &profile, const ExecutorConfig &config) {
  vector<vector<Op>> result;
  result.reserve(profile.extensions.size());
  for (auto &extension : profile.extensions) {
    auto ops = make_extension_ops(profile, extension, config);
    if (!ops.empty()) {
      result.push_back(std::move(ops));
    }
  }
  return result;
}

vector<Op> expand_template(const BrowserProfileSpec &profile, const ExecutorConfig &config) {
  vector<Op> result;
  for (auto &op : profile.layout_template) {
    switch (op.type) {
      case Op::Type::LegacyVersionFromProfile:
        result.push_back(Op::bytes(store_u16(static_cast<uint16>(profile.tls_version))));
        break;
      case Op::Type::CipherSuitesFromProfile:
        if (profile.grease.enabled) {
          result.push_back(Op::bytes(store_u16(to_uint16(profile.cipher_suites.size() * 2 + 2))));
          result.push_back(Op::grease(0));
        } else {
          result.push_back(Op::bytes(store_u16(to_uint16(profile.cipher_suites.size() * 2))));
        }
        result.push_back(Op::bytes(encode_u16_list(profile.cipher_suites)));
        break;
      case Op::Type::ExtensionsFromProfile: {
        for (auto &part : make_extension_parts(profile, config)) {
          result.insert(result.end(), part.begin(), part.end());
        }
        break;
      }
      case Op::Type::ExtensionsPermutationFromProfile:
        result.push_back(Op::permutation(make_extension_parts(profile, config)));
        break;
      default:
        result.push_back(op);
        break;
    }
  }
  return result;
}

}  // namespace

vector<ClientHelloOp> ClientHelloOpMapper::map(const BrowserProfileSpec &profile, const ExecutorConfig &config) {
  return expand_template(profile, config);
}

}  // namespace mtproto
}  // namespace td
