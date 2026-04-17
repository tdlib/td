// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/ClientHelloOp.h"

#include "td/utils/common.h"

namespace td {
namespace mtproto {

enum class BrowserProfile : uint8 {
  Chrome133,
  Chrome131,
  Chrome120,
  Chrome147_Windows,
  Chrome147_IOSChromium,
  Firefox148,
  Firefox149_MacOS26_3,
  Firefox149_Windows,
  Safari26_3,
  IOS14,
  Android11_OkHttp_Advisory,
};

enum class TlsVersion : uint16 {
  Tls12 = 0x0303,
  Tls13 = 0x0304,
};

enum class TlsExtensionType : uint16 {
  ServerName = 0,
  StatusRequest = 5,
  SupportedGroups = 10,
  EcPointFormats = 11,
  SignatureAlgorithms = 13,
  Alpn = 16,
  CompressCertificate = 27,
  RecordSizeLimit = 28,
  SignedCertificateTimestamp = 18,
  ExtendedMasterSecret = 23,
  DelegatedCredentials = 34,
  SessionTicket = 35,
  SupportedVersions = 43,
  PskKeyExchangeModes = 45,
  PreSharedKey = 41,
  KeyShare = 51,
  ApplicationSettings = 17613,
  EncryptedClientHello = 65037,
  RenegotiationInfo = 65281,
  Custom = 65535,
};

struct GreaseSettings {
  bool enabled{true};
  size_t value_count{7};
};

enum class KeyShareKind : uint16 {
  Grease = 0,
  X25519 = 29,
  Secp256r1 = 23,
  X25519MlKem768 = 4588,
};

struct KeyShareEntrySpec {
  KeyShareKind kind{KeyShareKind::X25519};
  // For `Grease` entries, this is the index into the executor's GREASE
  // value pool used to source the (group, group) byte pair on the wire.
  // Real Chrome / Safari / iOS captures use a single 1-byte body for
  // the GREASE entry, regardless of the GREASE codepoint chosen.
  uint8 grease_index{0};
};

struct BrowserExtension {
  TlsExtensionType type{TlsExtensionType::Custom};
  uint16 custom_type{0};
  bool is_dynamic{false};
  bool prepend_grease{false};
  string raw_data;
  vector<uint8> u8_list;
  vector<uint16> u16_list;
  vector<string> str_list;
  vector<KeyShareEntrySpec> key_share_entries;
};

struct BrowserProfileSpec {
  string name;
  TlsVersion tls_version{TlsVersion::Tls12};
  vector<uint16> cipher_suites;
  vector<uint16> supported_groups;
  vector<uint8> ec_point_formats;
  vector<string> alpn;
  GreaseSettings grease;
  vector<BrowserExtension> extensions;
  vector<ClientHelloOp> layout_template;
};

const BrowserProfileSpec &get_profile_spec(BrowserProfile profile_id);

}  // namespace mtproto
}  // namespace td
