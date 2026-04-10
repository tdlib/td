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
    case TlsExtensionType::SupportedGroups:
      ops = {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::scope16_begin()};
      if (extension.prepend_grease) {
        ops.push_back(Op::grease(4));
      }
      ops.push_back(Op::bytes(encode_u16_list(extension.u16_list)));
      ops.push_back(Op::scope16_end());
      ops.push_back(Op::scope16_end());
      return ops;
    case TlsExtensionType::EcPointFormats:
      return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::bytes(store_u8(to_uint8(extension.u8_list.size()))),
              Op::bytes(encode_u8_list(extension.u8_list)), Op::scope16_end()};
    case TlsExtensionType::SignatureAlgorithms:
    case TlsExtensionType::DelegatedCredentials:
      return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::scope16_begin(), Op::bytes(encode_u16_list(extension.u16_list)),
              Op::scope16_end(), Op::scope16_end()};
    case TlsExtensionType::Alpn:
      return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::scope16_begin(), Op::bytes(encode_str_list(extension.str_list)),
              Op::scope16_end(), Op::scope16_end()};
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
    case TlsExtensionType::EncryptedClientHello:
      if (config.has_ech) {
        return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::bytes("\x00\x00\x01\x00\x01"), Op::random_bytes(1),
                Op::bytes(store_u16(static_cast<uint16>(config.ech_enc_key_length))), Op::random_bytes(config.ech_enc_key_length),
                Op::scope16_begin(), Op::random_bytes(config.ech_payload_length),
                Op::scope16_end(), Op::scope16_end()};
      }
      return {};
    case TlsExtensionType::ApplicationSettings:
      if (config.alps_type != 0) {
        return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::bytes(store_u16(config.alps_type)),
                Op::bytes(extension.raw_data), Op::scope16_end()};
      }
      return {Op::bytes(store_u16(type)), Op::scope16_begin(), Op::bytes(extension.raw_data), Op::scope16_end()};
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
