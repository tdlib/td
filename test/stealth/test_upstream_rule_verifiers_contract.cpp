// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Contract and adversarial coverage for the UpstreamRuleVerifiers small
// classes. Every verifier gets a legal input that must pass and an
// illegal input that must be rejected.

#include "test/stealth/UpstreamRuleVerifiers.h"

#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::Slice;
using td::mtproto::test::ParsedKeyShareEntry;
using td::mtproto::test::verifiers::AlpsTypeVerifier;
using td::mtproto::test::verifiers::EchPayloadVerifier;
using td::mtproto::test::verifiers::ExtensionOrderVerifier;
using td::mtproto::test::verifiers::KeyShareStructureVerifier;

// ---------------------------------------------------------------------------
// ExtensionOrderVerifier
// ---------------------------------------------------------------------------

TEST(UpstreamRuleVerifiersContract, ExtensionOrderChromiumAnyPermutationPasses) {
  const auto &v = ExtensionOrderVerifier::get_for_family(Slice("chromium_linux_desktop"));
  td::vector<td::uint16> order_a = {0x0000u, 0x002Bu, 0x0033u, 0x000Du, 0x0005u, 0xFE0Du, 0x0010u};
  td::vector<td::uint16> order_b = {0x002Bu, 0x0000u, 0x0033u, 0x000Du, 0x0005u, 0xFE0Du, 0x0010u};
  td::vector<td::uint16> order_c = {0x0010u, 0x002Bu, 0x0000u, 0x0033u, 0x000Du, 0x0005u, 0xFE0Du};
  ASSERT_TRUE(v.is_legal_permutation(order_a));
  ASSERT_TRUE(v.is_legal_permutation(order_b));
  ASSERT_TRUE(v.is_legal_permutation(order_c));
}

TEST(UpstreamRuleVerifiersContract, ExtensionOrderChromiumEmptyRejected) {
  const auto &v = ExtensionOrderVerifier::get_for_family(Slice("chromium_windows"));
  td::vector<td::uint16> empty;
  ASSERT_FALSE(v.is_legal_permutation(empty));
}

TEST(UpstreamRuleVerifiersContract, ExtensionOrderDuplicateTypeFails) {
  const auto &v = ExtensionOrderVerifier::get_for_family(Slice("android_chromium"));
  td::vector<td::uint16> order = {0x0000u, 0x0033u, 0x0033u, 0x0010u};
  ASSERT_FALSE(v.is_legal_permutation(order));
}

TEST(UpstreamRuleVerifiersContract, ExtensionOrderAppleTlsAnyPermutationPasses) {
  const auto &v = ExtensionOrderVerifier::get_for_family(Slice("apple_ios_tls"));
  td::vector<td::uint16> order = {0x0017u, 0x0000u, 0xFF01u, 0x0010u, 0x000Au};
  ASSERT_TRUE(v.is_legal_permutation(order));
}

TEST(UpstreamRuleVerifiersContract, ExtensionOrderFirefoxAnyOrderAcceptedByVerifier) {
  // Firefox's fixed-order constraint is enforced at the higher-level
  // matcher; the verifier only rejects upstream-forbidden types, of
  // which there are currently none.
  const auto &v = ExtensionOrderVerifier::get_for_family(Slice("firefox_linux_desktop"));
  td::vector<td::uint16> order = {0x0000u, 0x002Bu, 0x0033u};
  ASSERT_TRUE(v.is_legal_permutation(order));
}

// ---------------------------------------------------------------------------
// KeyShareStructureVerifier
// ---------------------------------------------------------------------------

TEST(UpstreamRuleVerifiersContract, KeyShareChromiumPqFirstX25519Legal) {
  const auto &v = KeyShareStructureVerifier::get_for_family(Slice("chromium_linux_desktop"));
  td::vector<ParsedKeyShareEntry> entries = {
      {0x11ECu, 1216u, Slice()},
      {0x001Du, 32u, Slice()},
  };
  ASSERT_TRUE(v.is_legal_structure(entries));
}

TEST(UpstreamRuleVerifiersContract, KeyShareChromiumGreasePrefixAllowed) {
  const auto &v = KeyShareStructureVerifier::get_for_family(Slice("chromium_windows"));
  td::vector<ParsedKeyShareEntry> entries = {
      {0xDADAu, 1u, Slice()},  // GREASE prefix
      {0x11ECu, 1216u, Slice()},
      {0x001Du, 32u, Slice()},
  };
  ASSERT_TRUE(v.is_legal_structure(entries));
}

TEST(UpstreamRuleVerifiersContract, KeyShareChromiumPqNotFirstRejected) {
  const auto &v = KeyShareStructureVerifier::get_for_family(Slice("chromium_linux_desktop"));
  td::vector<ParsedKeyShareEntry> entries = {
      {0x001Du, 32u, Slice()},
      {0x11ECu, 1216u, Slice()},
  };
  ASSERT_FALSE(v.is_legal_structure(entries));
}

TEST(UpstreamRuleVerifiersContract, KeyShareChromiumBadPqLengthRejected) {
  const auto &v = KeyShareStructureVerifier::get_for_family(Slice("chromium_macos"));
  td::vector<ParsedKeyShareEntry> entries = {
      {0x11ECu, 1200u, Slice()},  // wrong length
      {0x001Du, 32u, Slice()},
  };
  ASSERT_FALSE(v.is_legal_structure(entries));
}

TEST(UpstreamRuleVerifiersContract, KeyShareFirefoxSecp256r1EntryLegal) {
  const auto &v = KeyShareStructureVerifier::get_for_family(Slice("firefox_linux_desktop"));
  td::vector<ParsedKeyShareEntry> entries = {
      {0x11ECu, 1216u, Slice()},
      {0x001Du, 32u, Slice()},
      {0x0017u, 65u, Slice()},
  };
  ASSERT_TRUE(v.is_legal_structure(entries));
}

TEST(UpstreamRuleVerifiersContract, KeyShareForeignGroupRejected) {
  const auto &v = KeyShareStructureVerifier::get_for_family(Slice("chromium_linux_desktop"));
  td::vector<ParsedKeyShareEntry> entries = {
      {0x11ECu, 1216u, Slice()},
      {0x0018u, 97u, Slice()},  // secp384r1, not in Chromium's legal set
  };
  ASSERT_FALSE(v.is_legal_structure(entries));
}

TEST(UpstreamRuleVerifiersContract, KeyShareGreaseWithNonOneLengthRejected) {
  const auto &v = KeyShareStructureVerifier::get_for_family(Slice("chromium_linux_desktop"));
  td::vector<ParsedKeyShareEntry> entries = {
      {0xDADAu, 32u, Slice()},  // GREASE must be exactly 1 byte
      {0x11ECu, 1216u, Slice()},
      {0x001Du, 32u, Slice()},
  };
  ASSERT_FALSE(v.is_legal_structure(entries));
}

// ---------------------------------------------------------------------------
// EchPayloadVerifier
// ---------------------------------------------------------------------------

TEST(UpstreamRuleVerifiersContract, EchChromiumAllowedBucketsPass) {
  const auto &v = EchPayloadVerifier::get_for_family(Slice("chromium_linux_desktop"));
  ASSERT_TRUE(v.family_advertises_ech());
  ASSERT_TRUE(v.is_legal_ech_payload_length(144));
  ASSERT_TRUE(v.is_legal_ech_payload_length(176));
  ASSERT_TRUE(v.is_legal_ech_payload_length(208));
  ASSERT_TRUE(v.is_legal_ech_payload_length(240));
}

TEST(UpstreamRuleVerifiersContract, EchChromiumUnrelatedLengthRejected) {
  const auto &v = EchPayloadVerifier::get_for_family(Slice("chromium_windows"));
  ASSERT_FALSE(v.is_legal_ech_payload_length(200));
  ASSERT_FALSE(v.is_legal_ech_payload_length(239));
  ASSERT_FALSE(v.is_legal_ech_payload_length(0));
}

TEST(UpstreamRuleVerifiersContract, EchChromiumAeadKdfPairContract) {
  const auto &v = EchPayloadVerifier::get_for_family(Slice("chromium_linux_desktop"));
  ASSERT_TRUE(v.is_legal_ech_aead_kdf_pair(0x0001u, 0x0001u));
  ASSERT_FALSE(v.is_legal_ech_aead_kdf_pair(0x0001u, 0x0002u));
  ASSERT_FALSE(v.is_legal_ech_aead_kdf_pair(0x0002u, 0x0001u));
}

TEST(UpstreamRuleVerifiersContract, EchFirefoxFixedLengthsPass) {
  const auto &v = EchPayloadVerifier::get_for_family(Slice("firefox_linux_desktop"));
  ASSERT_TRUE(v.family_advertises_ech());
  ASSERT_TRUE(v.is_legal_ech_payload_length(239));
  ASSERT_TRUE(v.is_legal_ech_payload_length(399));
}

TEST(UpstreamRuleVerifiersContract, EchFirefoxAeadSetPass) {
  const auto &v = EchPayloadVerifier::get_for_family(Slice("firefox_windows"));
  ASSERT_TRUE(v.is_legal_ech_aead_kdf_pair(0x0001u, 0x0001u));
  ASSERT_TRUE(v.is_legal_ech_aead_kdf_pair(0x0001u, 0x0003u));
}

TEST(UpstreamRuleVerifiersContract, EchAppleTlsDoesNotAdvertise) {
  const auto &v = EchPayloadVerifier::get_for_family(Slice("apple_ios_tls"));
  ASSERT_FALSE(v.family_advertises_ech());
  ASSERT_FALSE(v.is_legal_ech_payload_length(144));
  ASSERT_FALSE(v.is_legal_ech_aead_kdf_pair(0x0001u, 0x0001u));
}

// ---------------------------------------------------------------------------
// AlpsTypeVerifier
// ---------------------------------------------------------------------------

TEST(UpstreamRuleVerifiersContract, AlpsChromium133Uses44CD) {
  const auto &v = AlpsTypeVerifier::get_for_family(Slice("chromium_linux_desktop"));
  ASSERT_TRUE(v.family_advertises_alps());
  ASSERT_TRUE(v.is_legal_alps_type(0x44CDu, Slice("chrome133_plus")));
  ASSERT_FALSE(v.is_legal_alps_type(0x4469u, Slice("chrome133_plus")));
}

TEST(UpstreamRuleVerifiersContract, AlpsChromium131Uses4469) {
  const auto &v = AlpsTypeVerifier::get_for_family(Slice("android_chromium"));
  ASSERT_TRUE(v.is_legal_alps_type(0x4469u, Slice("chrome131")));
  ASSERT_FALSE(v.is_legal_alps_type(0x44CDu, Slice("chrome131")));
}

TEST(UpstreamRuleVerifiersContract, AlpsForeignTypeRejected) {
  const auto &v = AlpsTypeVerifier::get_for_family(Slice("chromium_windows"));
  ASSERT_FALSE(v.is_legal_alps_type(0x1234u, Slice()));
  ASSERT_FALSE(v.is_legal_alps_type(0xFE0Du, Slice()));
}

TEST(UpstreamRuleVerifiersContract, AlpsFirefoxNeverLegal) {
  const auto &v = AlpsTypeVerifier::get_for_family(Slice("firefox_linux_desktop"));
  ASSERT_FALSE(v.family_advertises_alps());
  ASSERT_FALSE(v.is_legal_alps_type(0x44CDu, Slice()));
  ASSERT_FALSE(v.is_legal_alps_type(0x4469u, Slice()));
}

TEST(UpstreamRuleVerifiersContract, AlpsAppleTlsNeverLegal) {
  const auto &v = AlpsTypeVerifier::get_for_family(Slice("apple_ios_tls"));
  ASSERT_FALSE(v.family_advertises_alps());
  ASSERT_FALSE(v.is_legal_alps_type(0x44CDu, Slice()));
}

}  // namespace
