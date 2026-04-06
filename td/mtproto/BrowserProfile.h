#pragma once

#include "td/mtproto/ClientHelloOp.h"

#include "td/utils/common.h"

namespace td {
namespace mtproto {

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
  SignedCertificateTimestamp = 18,
  ExtendedMasterSecret = 23,
  RecordSizeLimit = 28,
  CompressCertificate = 27,
  DelegatedCredentials = 34,
  SessionTicket = 35,
  SupportedVersions = 43,
  PskKeyExchangeModes = 45,
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
  X25519 = 29,
  Secp256r1 = 23,
  X25519MlKem768 = 4588,
};

struct KeyShareEntrySpec {
  KeyShareKind kind{KeyShareKind::X25519};
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

struct BrowserProfile {
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

const BrowserProfile &get_chrome_profile();
const BrowserProfile &get_chrome_darwin_profile();
const BrowserProfile &get_firefox_profile();

}  // namespace mtproto
}  // namespace td
