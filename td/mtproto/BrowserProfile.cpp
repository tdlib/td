#include "td/mtproto/BrowserProfile.h"

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

vector<Op> make_chrome_desktop_layout() {
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

vector<Op> make_chrome_darwin_layout() {
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

BrowserProfile make_chrome_profile_impl() {
  BrowserProfile profile;
  profile.name = "chrome_146";
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
      make_u16_extension(TlsExtensionType::SignatureAlgorithms, {0x0403, 0x0804, 0x0401, 0x0503, 0x0805}),
      make_string_extension(TlsExtensionType::Alpn, {"h2", "http/1.1"}),
      make_u16_extension(TlsExtensionType::SupportedVersions, {0x0304, 0x0303}, true),
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
  profile.layout_template = make_chrome_desktop_layout();
  return profile;
}

BrowserProfile make_chrome_darwin_profile_impl() {
  BrowserProfile profile;
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
                         {0x0403, 0x0804, 0x0401, 0x0503, 0x0805, 0x0501, 0x0806, 0x0601, 0x0201}),
      make_extension(TlsExtensionType::SignedCertificateTimestamp),
      make_key_share_extension({KeyShareKind::X25519}),
      make_u8_extension(TlsExtensionType::PskKeyExchangeModes, {1}),
      make_u16_extension(TlsExtensionType::SupportedVersions, {0x0304, 0x0303, 0x0302, 0x0301}, true),
      make_raw_extension(TlsExtensionType::CompressCertificate, "\x02\x00\x01"),
  };
  profile.layout_template = make_chrome_darwin_layout();
  return profile;
}

BrowserProfile make_firefox_profile_impl() {
  BrowserProfile profile;
  profile.name = "firefox149";
  profile.tls_version = TlsVersion::Tls12;
  profile.cipher_suites = {4865, 4867, 4866, 49195, 49199, 52393, 52392, 49196, 49200, 49162, 49161, 49171, 49172,
                           156, 157, 47, 53};
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
      make_u16_extension(TlsExtensionType::DelegatedCredentials, {0x0403, 0x0503, 0x0603, 0x0203}),
      make_extension(TlsExtensionType::SignedCertificateTimestamp),
      make_key_share_extension({KeyShareKind::X25519MlKem768, KeyShareKind::X25519, KeyShareKind::Secp256r1}),
      make_u16_extension(TlsExtensionType::SupportedVersions, {0x0304, 0x0303}),
      make_u16_extension(TlsExtensionType::SignatureAlgorithms,
                         {0x0403, 0x0503, 0x0603, 0x0804, 0x0805, 0x0806, 0x0401, 0x0501, 0x0601, 0x0203, 0x0201}),
      make_u8_extension(TlsExtensionType::PskKeyExchangeModes, {1}),
      make_custom_extension(28, "\x01\x00"),
      make_raw_extension(TlsExtensionType::CompressCertificate, "\x02\x00\x02"),
      make_extension(TlsExtensionType::EncryptedClientHello, true),
  };
  profile.layout_template = make_firefox_layout();
  return profile;
}

}  // namespace

const BrowserProfile &get_chrome_profile() {
  static const BrowserProfile profile = make_chrome_profile_impl();
  return profile;
}

const BrowserProfile &get_chrome_darwin_profile() {
  static const BrowserProfile profile = make_chrome_darwin_profile_impl();
  return profile;
}

const BrowserProfile &get_firefox_profile() {
  static const BrowserProfile profile = make_firefox_profile_impl();
  return profile;
}

}  // namespace mtproto
}  // namespace td
