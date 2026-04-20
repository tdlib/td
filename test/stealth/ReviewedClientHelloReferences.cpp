// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

#include "test/stealth/ReviewedClientHelloReferences.h"

#include "test/stealth/ReviewedClientHelloFixtures.h"

namespace td {
namespace mtproto {
namespace test {
namespace fixtures {
namespace reviewed_refs {

using namespace td::mtproto::test::fixtures::reviewed;

const vector<string> chrome146_177_android16_alpn_protocols = chrome146_177_android16AlpnProtocols;
const vector<string> chrome146_177_linux_desktop_alpn_protocols = chrome146_177_linux_desktopAlpnProtocols;

const vector<uint16> chrome146_75_linux_desktop_non_grease_extensions_without_padding =
    chrome146_75_linux_desktopNonGreaseExtensionsWithoutPadding;
const vector<uint16> firefox148_linux_desktop_non_grease_extensions_without_padding =
    firefox148_linux_desktopNonGreaseExtensionsWithoutPadding;

const vector<uint16> safari26_3_1_ios26_3_1_a_non_grease_cipher_suites = safari26_3_1_ios26_3_1_aNonGreaseCipherSuites;
const vector<uint16> safari26_3_1_ios26_3_1_a_non_grease_supported_groups =
    safari26_3_1_ios26_3_1_aNonGreaseSupportedGroups;
const vector<uint16> safari26_3_1_ios26_3_1_a_non_grease_extensions_without_padding =
    safari26_3_1_ios26_3_1_aNonGreaseExtensionsWithoutPadding;

const uint8 firefox149_macos26_3_ech_outer_type = firefox149_macos26_3Ech.outer_type;
const uint16 firefox149_macos26_3_ech_kdf_id = firefox149_macos26_3Ech.kdf_id;
const uint16 firefox149_macos26_3_ech_aead_id = firefox149_macos26_3Ech.aead_id;
const uint16 firefox149_macos26_3_ech_enc_length = firefox149_macos26_3Ech.enc_length;
const uint16 firefox149_macos26_3_ech_payload_length = firefox149_macos26_3Ech.payload_length;

const vector<uint16> chrome_linux_desktop_ref_non_grease_cipher_suites =
    kChromeLinuxDesktopReferenceNonGreaseCipherSuites;
const vector<uint16> chrome_linux_desktop_ref_non_grease_supported_groups =
    kChromeLinuxDesktopReferenceNonGreaseSupportedGroups;
const vector<uint16> chrome_linux_desktop_ref_non_grease_extensions_without_padding =
    kChromeLinuxDesktopReferenceNonGreaseExtensionsWithoutPadding;

const vector<uint16> firefox_linux_desktop_ref_cipher_suites = kFirefoxLinuxDesktopReferenceCipherSuites;
const vector<uint16> firefox_linux_desktop_ref_supported_groups = kFirefoxLinuxDesktopReferenceSupportedGroups;
const vector<uint16> firefox_linux_desktop_ref_extension_order = kFirefoxLinuxDesktopReferenceExtensionOrder;
const size_t firefox_linux_desktop_ref_key_share_entry_count = kFirefoxLinuxDesktopReferenceKeyShareEntries.size();
const uint8 firefox_linux_desktop_ref_ech_outer_type = kFirefoxLinuxDesktopReferenceEch.outer_type;
const uint16 firefox_linux_desktop_ref_ech_kdf_id = kFirefoxLinuxDesktopReferenceEch.kdf_id;
const uint16 firefox_linux_desktop_ref_ech_aead_id = kFirefoxLinuxDesktopReferenceEch.aead_id;
const uint16 firefox_linux_desktop_ref_ech_enc_length = kFirefoxLinuxDesktopReferenceEch.enc_length;
const uint16 firefox_linux_desktop_ref_ech_payload_length = kFirefoxLinuxDesktopReferenceEch.payload_length;

const vector<uint16> firefox149_linux_desktop_ref_supported_groups = kFirefox149LinuxDesktopReferenceSupportedGroups;
const vector<uint16> firefox149_linux_desktop_ref_extension_order = kFirefox149LinuxDesktopReferenceExtensionOrder;
const size_t firefox149_linux_desktop_ref_key_share_entry_count =
    kFirefox149LinuxDesktopReferenceKeyShareEntries.size();
const uint8 firefox149_linux_desktop_ref_ech_outer_type = kFirefox149LinuxDesktopReferenceEch.outer_type;
const uint16 firefox149_linux_desktop_ref_ech_kdf_id = kFirefox149LinuxDesktopReferenceEch.kdf_id;
const uint16 firefox149_linux_desktop_ref_ech_aead_id = kFirefox149LinuxDesktopReferenceEch.aead_id;
const uint16 firefox149_linux_desktop_ref_ech_enc_length = kFirefox149LinuxDesktopReferenceEch.enc_length;
const uint16 firefox149_linux_desktop_ref_ech_payload_length = kFirefox149LinuxDesktopReferenceEch.payload_length;

}  // namespace reviewed_refs
}  // namespace fixtures
}  // namespace test
}  // namespace mtproto
}  // namespace td
