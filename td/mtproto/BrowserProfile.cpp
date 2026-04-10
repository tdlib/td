// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/BrowserProfile.h"

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/Slice.h"

namespace td {
namespace mtproto {

namespace {

using Op = ClientHelloOp;

BrowserExtension make_extension(TlsExtensionType type, bool is_dynamic = false) {
  BrowserExtension extension;
  extension.type = type;
  extension.is_dynamic = is_dynamic;
  return extension;
}

BrowserExtension make_raw_extension(TlsExtensionType type, Slice raw_data) {
  BrowserExtension extension;
  extension.type = type;
  extension.raw_data = raw_data.str();
  return extension;
}

BrowserExtension make_custom_extension(uint16 custom_type, Slice raw_data) {
  BrowserExtension extension;
  extension.type = TlsExtensionType::Custom;
  extension.custom_type = custom_type;
  extension.raw_data = raw_data.str();
  return extension;
}

BrowserExtension make_u8_extension(TlsExtensionType type, vector<uint8> values) {
  BrowserExtension extension;
  extension.type = type;
  extension.u8_list = std::move(values);
  return extension;
}

BrowserExtension make_u16_extension(TlsExtensionType type, vector<uint16> values, bool prepend_grease = false) {
  BrowserExtension extension;
  extension.type = type;
  extension.u16_list = std::move(values);
  extension.prepend_grease = prepend_grease;
  return extension;
}

BrowserExtension make_string_extension(TlsExtensionType type, vector<string> values) {
  BrowserExtension extension;
  extension.type = type;
  extension.str_list = std::move(values);
  return extension;
}

BrowserExtension make_key_share_extension(std::initializer_list<KeyShareKind> entries) {
  BrowserExtension extension;
  extension.type = TlsExtensionType::KeyShare;
  extension.is_dynamic = true;
  for (auto entry : entries) {
    extension.key_share_entries.push_back(KeyShareEntrySpec{entry});
  }
  return extension;
}

vector<Op> make_chromium_desktop_layout() {
  return {
      Op::bytes("\x16\x03\x01"),
      Op::scope16_begin(),
      Op::bytes("\x01\x00"),
      Op::scope16_begin(),
      Op::legacy_version_from_profile(),
      Op::zero_bytes(32),
      Op::bytes("\x20"),
      Op::random_bytes(32),
      Op::cipher_suites_from_profile(),
      Op::bytes("\x01\x00"),
      Op::scope16_begin(),
      Op::grease(2),
      Op::bytes("\x00\x00"),
      Op::extensions_permutation_from_profile(),
      Op::grease(3),
      Op::bytes("\x00\x01\x00"),
      Op::padding_to_target(513),
      Op::scope16_end(),
      Op::scope16_end(),
      Op::scope16_end(),
  };
}

vector<Op> make_chromium_darwin_layout() {
  return {
      Op::bytes("\x16\x03\x01\x02\x00\x01\x00\x01\xfc"),
      Op::legacy_version_from_profile(),
      Op::zero_bytes(32),
      Op::bytes("\x20"),
      Op::random_bytes(32),
      Op::cipher_suites_from_profile(),
      Op::grease(2),
      Op::bytes("\x00\x00"),
      Op::extensions_from_profile(),
      Op::grease(3),
      Op::bytes("\x00\x01\x00"),
      Op::padding_to_target(513),
  };
}

vector<Op> make_safari_layout() {
  return {
      Op::bytes("\x16\x03\x01"),
      Op::scope16_begin(),
      Op::bytes("\x01\x00"),
      Op::scope16_begin(),
      Op::legacy_version_from_profile(),
      Op::zero_bytes(32),
      Op::bytes("\x20"),
      Op::random_bytes(32),
      Op::cipher_suites_from_profile(),
      Op::bytes("\x01\x00"),
      Op::scope16_begin(),
      Op::grease(2),
      Op::bytes("\x00\x00"),
      Op::extensions_from_profile(),
      Op::scope16_end(),
      Op::scope16_end(),
      Op::scope16_end(),
  };
}

vector<Op> make_ios_layout() {
  return {
      Op::bytes("\x16\x03\x01"),
      Op::scope16_begin(),
      Op::bytes("\x01\x00"),
      Op::scope16_begin(),
      Op::legacy_version_from_profile(),
      Op::zero_bytes(32),
      Op::bytes("\x20"),
      Op::random_bytes(32),
      Op::cipher_suites_from_profile(),
      Op::bytes("\x01\x00"),
      Op::scope16_begin(),
      Op::grease(2),
      Op::bytes("\x00\x00"),
      Op::extensions_from_profile(),
      Op::scope16_end(),
      Op::scope16_end(),
      Op::scope16_end(),
  };
}

vector<Op> make_firefox_layout() {
  return {
      Op::bytes("\x16\x03\x01"),
      Op::scope16_begin(),
      Op::bytes("\x01\x00"),
      Op::scope16_begin(),
      Op::legacy_version_from_profile(),
      Op::zero_bytes(32),
      Op::bytes("\x20"),
      Op::random_bytes(32),
      Op::cipher_suites_from_profile(),
      Op::bytes("\x01\x00"),
      Op::scope16_begin(),
      Op::extensions_from_profile(),
      Op::scope16_end(),
      Op::scope16_end(),
      Op::scope16_end(),
  };
}

vector<Op> make_firefox_ech_layout() {
  return {
      Op::bytes("\x16\x03\x01"),
      Op::scope16_begin(),
      Op::bytes("\x01\x00"),
      Op::scope16_begin(),
      Op::legacy_version_from_profile(),
      Op::zero_bytes(32),
      Op::bytes("\x20"),
      Op::random_bytes(32),
      Op::cipher_suites_from_profile(),
      Op::bytes("\x01\x00"),
      Op::scope16_begin(),
      Op::extensions_from_profile(),
      Op::scope16_end(),
      Op::scope16_end(),
      Op::scope16_end(),
  };
}

BrowserProfileSpec make_chrome133_impl() {
  BrowserProfileSpec profile;
  profile.name = "chrome133";
  profile.tls_version = TlsVersion::Tls12;
  profile.cipher_suites = {4865, 4866, 4867, 49195, 49199, 49196, 49200, 52393, 52392, 49171, 49172, 156, 157, 47, 53, 10};
  profile.supported_groups = {4588, 29, 23, 24};
  profile.ec_point_formats = {0};
  profile.alpn = {"h2", "http/1.1"};
  profile.grease = {true, 7};
  profile.extensions = {
      make_extension(TlsExtensionType::ServerName, true),
      make_u16_extension(TlsExtensionType::SupportedGroups, {4588, 29, 23, 24}, true),
      make_u8_extension(TlsExtensionType::EcPointFormats, {0}),
      make_u16_extension(TlsExtensionType::SignatureAlgorithms, {1027, 1283, 1025, 1281, 513, 515}),
      make_string_extension(TlsExtensionType::Alpn, {"h2", "http/1.1"}),
      make_u16_extension(TlsExtensionType::SupportedVersions, {768, 771}, true),
      make_u8_extension(TlsExtensionType::PskKeyExchangeModes, {1}),
      make_key_share_extension({KeyShareKind::X25519MlKem768, KeyShareKind::X25519}),
      make_raw_extension(TlsExtensionType::StatusRequest, "\x01\x00\x00\x00\x00"),
      make_extension(TlsExtensionType::SignedCertificateTimestamp),
      make_extension(TlsExtensionType::ExtendedMasterSecret),
      make_extension(TlsExtensionType::SessionTicket),
      make_raw_extension(TlsExtensionType::RenegotiationInfo, "\x00"),
      make_raw_extension(TlsExtensionType::ApplicationSettings, "\x00\x03\x02\x68\x32"),
      make_extension(TlsExtensionType::EncryptedClientHello, true),
  };
  profile.layout_template = make_chromium_desktop_layout();
  return profile;
}

BrowserProfileSpec make_chrome131_impl() {
  BrowserProfileSpec profile;
  profile.name = "chrome131";
  profile.tls_version = TlsVersion::Tls12;
  profile.cipher_suites = {4865, 4866, 4867, 49195, 49199, 49196, 49200, 52393, 52392, 49171, 49172, 156, 157, 47, 53, 10};
  profile.supported_groups = {4588, 29, 23, 24};
  profile.ec_point_formats = {0};
  profile.alpn = {"h2", "http/1.1"};
  profile.grease = {true, 7};
  profile.extensions = {
      make_extension(TlsExtensionType::ServerName, true),
      make_u16_extension(TlsExtensionType::SupportedGroups, {4588, 29, 23, 24}, true),
      make_u8_extension(TlsExtensionType::EcPointFormats, {0}),
      make_u16_extension(TlsExtensionType::SignatureAlgorithms, {1027, 1283, 1025, 1281, 513, 515}),
      make_string_extension(TlsExtensionType::Alpn, {"h2", "http/1.1"}),
      make_u16_extension(TlsExtensionType::SupportedVersions, {768, 771}, true),
      make_u8_extension(TlsExtensionType::PskKeyExchangeModes, {1}),
      make_key_share_extension({KeyShareKind::X25519MlKem768, KeyShareKind::X25519}),
      make_raw_extension(TlsExtensionType::StatusRequest, "\x01\x00\x00\x00\x00"),
      make_extension(TlsExtensionType::SignedCertificateTimestamp),
      make_extension(TlsExtensionType::ExtendedMasterSecret),
      make_extension(TlsExtensionType::SessionTicket),
      make_raw_extension(TlsExtensionType::RenegotiationInfo, "\x00"),
      make_raw_extension(TlsExtensionType::ApplicationSettings, "\x00\x03\x02\x68\x32"),
      make_extension(TlsExtensionType::EncryptedClientHello, true),
  };
  profile.layout_template = make_chromium_desktop_layout();
  return profile;
}

BrowserProfileSpec make_chrome120_impl() {
  BrowserProfileSpec profile;
  profile.name = "chrome120";
  profile.tls_version = TlsVersion::Tls12;
  profile.cipher_suites = {4865, 4866, 4867, 49195, 49199, 49196, 49200, 52393, 52392, 49171, 49172, 156, 157, 47, 53, 10};
  profile.supported_groups = {29, 23, 24};
  profile.ec_point_formats = {0};
  profile.alpn = {"h2", "http/1.1"};
  profile.grease = {true, 7};
  profile.extensions = {
      make_extension(TlsExtensionType::ServerName, true),
      make_u16_extension(TlsExtensionType::SupportedGroups, {29, 23, 24}, true),
      make_u8_extension(TlsExtensionType::EcPointFormats, {0}),
      make_u16_extension(TlsExtensionType::SignatureAlgorithms, {1027, 1283, 1025, 1281, 513, 515}),
      make_string_extension(TlsExtensionType::Alpn, {"h2", "http/1.1"}),
      make_u16_extension(TlsExtensionType::SupportedVersions, {768, 771}, true),
      make_u8_extension(TlsExtensionType::PskKeyExchangeModes, {1}),
      make_key_share_extension({KeyShareKind::X25519}),
      make_raw_extension(TlsExtensionType::StatusRequest, "\x01\x00\x00\x00\x00"),
      make_extension(TlsExtensionType::SignedCertificateTimestamp),
      make_extension(TlsExtensionType::ExtendedMasterSecret),
      make_extension(TlsExtensionType::SessionTicket),
      make_raw_extension(TlsExtensionType::RenegotiationInfo, "\x00"),
      make_raw_extension(TlsExtensionType::ApplicationSettings, "\x00\x03\x02\x68\x32"),
      make_extension(TlsExtensionType::EncryptedClientHello, true),
  };
  profile.layout_template = make_chromium_desktop_layout();
  return profile;
}

BrowserProfileSpec make_chrome_darwin_impl() {
  BrowserProfileSpec profile;
  profile.name = "chrome_darwin";
  profile.tls_version = TlsVersion::Tls12;
  profile.cipher_suites = {4865, 4866, 4867, 49196, 49195, 52393, 49200, 49199, 52392, 49162, 49161, 49172,
                           49171, 157, 156, 53, 47, 49160, 49170, 10};
  profile.supported_groups = {29, 23, 24, 25};
  profile.ec_point_formats = {0};
  profile.alpn = {"h2", "http/1.1"};
  profile.grease = {true, 7};
  profile.extensions = {
      make_extension(TlsExtensionType::ServerName, true),
      make_extension(TlsExtensionType::ExtendedMasterSecret),
      make_raw_extension(TlsExtensionType::RenegotiationInfo, "\x00"),
      make_u16_extension(TlsExtensionType::SupportedGroups, {29, 23, 24, 25}, true),
      make_u8_extension(TlsExtensionType::EcPointFormats, {0}),
      make_string_extension(TlsExtensionType::Alpn, {"h2", "http/1.1"}),
      make_raw_extension(TlsExtensionType::StatusRequest, "\x01\x00\x00\x00\x00"),
      make_u16_extension(TlsExtensionType::SignatureAlgorithms,
                         {1027, 1284, 1025, 1283, 1281, 1537, 1282, 1026}),
      make_extension(TlsExtensionType::SignedCertificateTimestamp),
      make_key_share_extension({KeyShareKind::X25519}),
      make_u8_extension(TlsExtensionType::PskKeyExchangeModes, {1}),
      make_u16_extension(TlsExtensionType::SupportedVersions, {768, 771, 770, 769}, true),
      make_raw_extension(TlsExtensionType::CompressCertificate, "\x02\x00\x01"),
  };
  profile.layout_template = make_chromium_darwin_layout();
  return profile;
}

BrowserProfileSpec make_firefox148_impl() {
  BrowserProfileSpec profile;
  profile.name = "firefox148";
  profile.tls_version = TlsVersion::Tls12;
  profile.cipher_suites = {4865, 4867, 4866, 49195, 49199, 52393, 52392, 49196, 49200, 49162, 49161, 49171, 49172, 156, 157, 47, 53};
  profile.supported_groups = {4588, 29, 23, 24, 25, 256, 257};
  profile.ec_point_formats = {0};
  profile.alpn = {"h2", "http/1.1"};
  profile.grease = {false, 0};
  profile.extensions = {
      make_extension(TlsExtensionType::ServerName, true),
      make_extension(TlsExtensionType::ExtendedMasterSecret),
      make_raw_extension(TlsExtensionType::RenegotiationInfo, "\x00"),
      make_u16_extension(TlsExtensionType::SupportedGroups, {4588, 29, 23, 24, 25, 256, 257}),
      make_u8_extension(TlsExtensionType::EcPointFormats, {0}),
      make_extension(TlsExtensionType::SessionTicket),
      make_string_extension(TlsExtensionType::Alpn, {"h2", "http/1.1"}),
      make_raw_extension(TlsExtensionType::StatusRequest, "\x01\x00\x00\x00\x00"),
      make_u16_extension(TlsExtensionType::DelegatedCredentials, {1027, 1283, 1539, 515}),
      make_extension(TlsExtensionType::SignedCertificateTimestamp),
      make_key_share_extension({KeyShareKind::X25519MlKem768, KeyShareKind::X25519, KeyShareKind::Secp256r1}),
      make_u16_extension(TlsExtensionType::SupportedVersions, {768, 771}),
      make_u16_extension(TlsExtensionType::SignatureAlgorithms,
                         {1027, 1283, 1539, 1284, 1285, 1286, 1025, 1281, 1537, 515, 513}),
      make_u8_extension(TlsExtensionType::PskKeyExchangeModes, {1}),
      make_custom_extension(28, "\x01\x00"),
      make_raw_extension(TlsExtensionType::CompressCertificate, "\x02\x00\x02"),
      make_extension(TlsExtensionType::EncryptedClientHello, true),
  };
  profile.layout_template = make_firefox_layout();
  return profile;
}

BrowserProfileSpec make_firefox149_macos_impl() {
  BrowserProfileSpec profile;
  profile.name = "firefox149_macos26_3";
  profile.tls_version = TlsVersion::Tls12;
  profile.cipher_suites = {4865, 4867, 4866, 49195, 49199, 52393, 52392, 49196, 49200, 49162, 49161, 49171, 49172, 156, 157, 47, 53};
  profile.supported_groups = {4588, 29, 23, 24, 25, 256, 257};
  profile.ec_point_formats = {0};
  profile.alpn = {"h2", "http/1.1"};
  profile.grease = {false, 0};
  profile.extensions = {
      make_extension(TlsExtensionType::ServerName, true),
      make_extension(TlsExtensionType::ExtendedMasterSecret),
      make_raw_extension(TlsExtensionType::RenegotiationInfo, "\x00"),
      make_u16_extension(TlsExtensionType::SupportedGroups, {4588, 29, 23, 24, 25, 256, 257}),
      make_u8_extension(TlsExtensionType::EcPointFormats, {0}),
      make_extension(TlsExtensionType::SessionTicket),
      make_string_extension(TlsExtensionType::Alpn, {"h2", "http/1.1"}),
      make_raw_extension(TlsExtensionType::StatusRequest, "\x01\x00\x00\x00\x00"),
      make_u16_extension(TlsExtensionType::DelegatedCredentials, {1027, 1283, 1539, 515}),
      make_extension(TlsExtensionType::SignedCertificateTimestamp),
      make_key_share_extension({KeyShareKind::X25519MlKem768, KeyShareKind::X25519, KeyShareKind::Secp256r1}),
      make_u16_extension(TlsExtensionType::SupportedVersions, {768, 771}),
      make_u16_extension(TlsExtensionType::SignatureAlgorithms,
                         {1027, 1283, 1539, 1284, 1285, 1286, 1025, 1281, 1537, 515, 513}),
      make_u8_extension(TlsExtensionType::PskKeyExchangeModes, {1}),
      make_custom_extension(28, "\x01\x00"),
      make_raw_extension(TlsExtensionType::CompressCertificate, "\x02\x00\x02"),
      make_extension(TlsExtensionType::EncryptedClientHello, true),
  };
  profile.layout_template = make_firefox_ech_layout();
  return profile;
}

BrowserProfileSpec make_safari_impl() {
  BrowserProfileSpec profile;
  profile.name = "safari26_3";
  profile.tls_version = TlsVersion::Tls12;
  profile.cipher_suites = {4866, 4867, 4865, 49196, 49195, 52393, 49200, 49199, 52392, 49162, 49161, 49172, 49171, 157, 156, 53, 47, 49160, 49170, 10};
  profile.supported_groups = {29, 23, 24, 25};
  profile.ec_point_formats = {0};
  profile.alpn = {"h2", "http/1.1"};
  profile.grease = {true, 7};
  profile.extensions = {
      make_extension(TlsExtensionType::ServerName, true),
      make_extension(TlsExtensionType::ExtendedMasterSecret),
      make_raw_extension(TlsExtensionType::RenegotiationInfo, "\x00"),
      make_u16_extension(TlsExtensionType::SupportedGroups, {29, 23, 24, 25}, true),
      make_u8_extension(TlsExtensionType::EcPointFormats, {0}),
      make_string_extension(TlsExtensionType::Alpn, {"h2", "http/1.1"}),
      make_raw_extension(TlsExtensionType::StatusRequest, "\x01\x00\x00\x00\x00"),
      make_u16_extension(TlsExtensionType::SignatureAlgorithms,
                         {1027, 1283, 1025, 1281, 513, 515, 1026, 1282, 1537}),
      make_extension(TlsExtensionType::SignedCertificateTimestamp),
      make_key_share_extension({KeyShareKind::X25519}),
      make_u8_extension(TlsExtensionType::PskKeyExchangeModes, {1}),
      make_u16_extension(TlsExtensionType::SupportedVersions, {768, 771, 770, 769}, true),
      make_raw_extension(TlsExtensionType::CompressCertificate, "\x02\x00\x01"),
  };
  profile.layout_template = make_safari_layout();
  return profile;
}

BrowserProfileSpec make_ios14_impl() {
  BrowserProfileSpec profile;
  profile.name = "ios14";
  profile.tls_version = TlsVersion::Tls12;
  profile.cipher_suites = {4866, 4867, 4865, 49196, 49195, 52393, 49200, 49199, 52392, 49162, 49161, 49172, 49171, 157, 156, 53, 47, 49160, 49170, 10};
  profile.supported_groups = {29, 23, 24, 25};
  profile.ec_point_formats = {0};
  profile.alpn = {"h2", "http/1.1"};
  profile.grease = {true, 7};
  profile.extensions = {
      make_extension(TlsExtensionType::ServerName, true),
      make_extension(TlsExtensionType::ExtendedMasterSecret),
      make_raw_extension(TlsExtensionType::RenegotiationInfo, "\x00"),
      make_u16_extension(TlsExtensionType::SupportedGroups, {29, 23, 24, 25}, true),
      make_u8_extension(TlsExtensionType::EcPointFormats, {0}),
      make_string_extension(TlsExtensionType::Alpn, {"h2", "http/1.1"}),
      make_raw_extension(TlsExtensionType::StatusRequest, "\x01\x00\x00\x00\x00"),
      make_u16_extension(TlsExtensionType::SignatureAlgorithms,
                         {1027, 1283, 1025, 1281, 513, 515, 1026, 1282, 1537}),
      make_extension(TlsExtensionType::SignedCertificateTimestamp),
      make_key_share_extension({KeyShareKind::X25519}),
      make_u8_extension(TlsExtensionType::PskKeyExchangeModes, {1}),
      make_u16_extension(TlsExtensionType::SupportedVersions, {768, 771, 770, 769}, true),
      make_raw_extension(TlsExtensionType::CompressCertificate, "\x02\x00\x01"),
  };
  profile.layout_template = make_ios_layout();
  return profile;
}

BrowserProfileSpec make_android_okhttp_impl() {
  BrowserProfileSpec profile;
  profile.name = "android11_okhttp_advisory";
  profile.tls_version = TlsVersion::Tls12;
  profile.cipher_suites = {4865, 4866, 4867, 49195, 49199, 49196, 49200, 52393, 52392, 49171, 49172, 156, 157, 47, 53};
  profile.supported_groups = {29, 23, 24};
  profile.ec_point_formats = {0};
  profile.alpn = {"h2", "http/1.1"};
  profile.grease = {true, 7};
  profile.extensions = {
      make_extension(TlsExtensionType::ServerName, true),
      make_u16_extension(TlsExtensionType::SupportedGroups, {29, 23, 24}, true),
      make_u8_extension(TlsExtensionType::EcPointFormats, {0}),
      make_u16_extension(TlsExtensionType::SignatureAlgorithms, {1027, 1283, 1025, 1281, 513, 515}),
      make_string_extension(TlsExtensionType::Alpn, {"h2", "http/1.1"}),
      make_u16_extension(TlsExtensionType::SupportedVersions, {768, 771}, true),
      make_u8_extension(TlsExtensionType::PskKeyExchangeModes, {1}),
      make_key_share_extension({KeyShareKind::X25519}),
  };
  profile.layout_template = make_chromium_desktop_layout();
  return profile;
}

}  // namespace

const BrowserProfileSpec &get_profile_spec(BrowserProfile profile_id) {
  switch (profile_id) {
    case BrowserProfile::Chrome133: {
      static const BrowserProfileSpec spec = make_chrome133_impl();
      return spec;
    }
    case BrowserProfile::Chrome131: {
      static const BrowserProfileSpec spec = make_chrome131_impl();
      return spec;
    }
    case BrowserProfile::Chrome120: {
      static const BrowserProfileSpec spec = make_chrome120_impl();
      return spec;
    }
    case BrowserProfile::Firefox148: {
      static const BrowserProfileSpec spec = make_firefox148_impl();
      return spec;
    }
    case BrowserProfile::Firefox149_MacOS26_3: {
      static const BrowserProfileSpec spec = make_firefox149_macos_impl();
      return spec;
    }
    case BrowserProfile::Safari26_3: {
      static const BrowserProfileSpec spec = make_safari_impl();
      return spec;
    }
    case BrowserProfile::IOS14: {
      static const BrowserProfileSpec spec = make_ios14_impl();
      return spec;
    }
    case BrowserProfile::Android11_OkHttp_Advisory: {
      static const BrowserProfileSpec spec = make_android_okhttp_impl();
      return spec;
    }
    default: {
      static const BrowserProfileSpec spec = make_chrome133_impl();
      return spec;
    }
  }
}

}  // namespace mtproto
}  // namespace td
