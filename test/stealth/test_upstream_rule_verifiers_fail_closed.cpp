// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Security contract: verifiers must fail closed for unknown family IDs.
// A permissive fallback lets unreviewed family labels pass legality gates,
// weakening corpus integrity and cross-family contamination checks.

#include "test/stealth/UpstreamRuleVerifiers.h"

#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::test::ParsedKeyShareEntry;
using td::mtproto::test::verifiers::AlpsTypeVerifier;
using td::mtproto::test::verifiers::EchPayloadVerifier;
using td::mtproto::test::verifiers::ExtensionOrderVerifier;
using td::mtproto::test::verifiers::KeyShareStructureVerifier;
using td::Slice;

TEST(UpstreamRuleVerifiersFailClosed, UnknownFamilyRejectsExtensionOrderLegality) {
  const auto &v = ExtensionOrderVerifier::get_for_family(Slice("unknown_family_lane"));
  td::vector<td::uint16> non_empty = {0x0000u, 0x002Bu, 0x0033u};
  td::vector<td::uint16> empty;
  ASSERT_FALSE(v.is_legal_permutation(non_empty));
  ASSERT_FALSE(v.is_legal_permutation(empty));
}

TEST(UpstreamRuleVerifiersFailClosed, UnknownFamilyRejectsKeyShareStructures) {
  const auto &v = KeyShareStructureVerifier::get_for_family(Slice("unknown_family_lane"));
  td::vector<ParsedKeyShareEntry> plausible = {
      {0x11ECu, 1216u, Slice()},
      {0x001Du, 32u, Slice()},
  };
  td::vector<ParsedKeyShareEntry> empty;
  ASSERT_FALSE(v.is_legal_structure(plausible));
  ASSERT_FALSE(v.is_legal_structure(empty));
}

TEST(UpstreamRuleVerifiersFailClosed, UnknownFamilyRejectsEchPayloadAndAeadKdfPairs) {
  const auto &v = EchPayloadVerifier::get_for_family(Slice("unknown_family_lane"));
  ASSERT_FALSE(v.family_advertises_ech());
  ASSERT_FALSE(v.is_legal_ech_payload_length(144));
  ASSERT_FALSE(v.is_legal_ech_aead_kdf_pair(0x0001u, 0x0001u));
}

TEST(UpstreamRuleVerifiersFailClosed, UnknownFamilyRejectsAlpsTypes) {
  const auto &v = AlpsTypeVerifier::get_for_family(Slice("unknown_family_lane"));
  ASSERT_FALSE(v.family_advertises_alps());
  ASSERT_FALSE(v.is_legal_alps_type(0x4469u, Slice()));
  ASSERT_FALSE(v.is_legal_alps_type(0x44CDu, Slice("chrome147")));
}

}  // namespace
