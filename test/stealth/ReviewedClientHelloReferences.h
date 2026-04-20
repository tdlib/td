// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Lightweight dynamic references to reviewed fixtures.
// Values are sourced in ReviewedClientHelloReferences.cpp from
// ReviewedClientHelloFixtures.h so they track the generated corpus.

#pragma once

#include "td/utils/common.h"

namespace td {
namespace mtproto {
namespace test {
namespace fixtures {
namespace reviewed_refs {

extern const vector<string> chrome146_177_android16_alpn_protocols;
extern const vector<string> chrome146_177_linux_desktop_alpn_protocols;

extern const vector<uint16> chrome146_75_linux_desktop_non_grease_extensions_without_padding;
extern const vector<uint16> firefox148_linux_desktop_non_grease_extensions_without_padding;

extern const vector<uint16> safari26_3_1_ios26_3_1_a_non_grease_cipher_suites;
extern const vector<uint16> safari26_3_1_ios26_3_1_a_non_grease_supported_groups;
extern const vector<uint16> safari26_3_1_ios26_3_1_a_non_grease_extensions_without_padding;

extern const uint8 firefox149_macos26_3_ech_outer_type;
extern const uint16 firefox149_macos26_3_ech_kdf_id;
extern const uint16 firefox149_macos26_3_ech_aead_id;
extern const uint16 firefox149_macos26_3_ech_enc_length;
extern const uint16 firefox149_macos26_3_ech_payload_length;

extern const vector<uint16> chrome_linux_desktop_ref_non_grease_cipher_suites;
extern const vector<uint16> chrome_linux_desktop_ref_non_grease_supported_groups;
extern const vector<uint16> chrome_linux_desktop_ref_non_grease_extensions_without_padding;

extern const vector<uint16> firefox_linux_desktop_ref_cipher_suites;
extern const vector<uint16> firefox_linux_desktop_ref_supported_groups;
extern const vector<uint16> firefox_linux_desktop_ref_extension_order;
extern const size_t firefox_linux_desktop_ref_key_share_entry_count;
extern const uint8 firefox_linux_desktop_ref_ech_outer_type;
extern const uint16 firefox_linux_desktop_ref_ech_kdf_id;
extern const uint16 firefox_linux_desktop_ref_ech_aead_id;
extern const uint16 firefox_linux_desktop_ref_ech_enc_length;
extern const uint16 firefox_linux_desktop_ref_ech_payload_length;

extern const vector<uint16> firefox149_linux_desktop_ref_supported_groups;
extern const vector<uint16> firefox149_linux_desktop_ref_extension_order;
extern const size_t firefox149_linux_desktop_ref_key_share_entry_count;
extern const uint8 firefox149_linux_desktop_ref_ech_outer_type;
extern const uint16 firefox149_linux_desktop_ref_ech_kdf_id;
extern const uint16 firefox149_linux_desktop_ref_ech_aead_id;
extern const uint16 firefox149_linux_desktop_ref_ech_enc_length;
extern const uint16 firefox149_linux_desktop_ref_ech_payload_length;

}  // namespace reviewed_refs
}  // namespace fixtures
}  // namespace test
}  // namespace mtproto
}  // namespace td
