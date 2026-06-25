// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

bool has(const td::string &s, td::Slice needle) {
  return s.find(needle.str()) != td::string::npos;
}

// Phase-2 instant-view RichText sub-batch (upstream cluster: 02e17a09d recurse_text, 4d10e3ae4
// RichText::get_full_text, ac0c191f3 get_input_rich_text, 99e233f6c/0bca63e05/5161b5958 remove the
// auto-* rich-text types, 65cd30a81 richTextBankCardNumber.bank_card_number, d17125721
// richTextReferenceLink). These pin the td_api schema END-STATE so a future merge cannot silently
// re-introduce the removed types or drop the new ones.
TEST(Phase2InstantViewSchemaContract, RichTextAutoTypesRemoved) {
  auto tl = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/generate/scheme/td_api.tl"));
  // the three "automatic" rich-text types were added then removed upstream; the fork must not carry them
  ASSERT_TRUE(!has(tl, "richTextAutoUrltext:RichText=RichText;"));
  ASSERT_TRUE(!has(tl, "richTextAutoEmailAddresstext:RichText=RichText;"));
  ASSERT_TRUE(!has(tl, "richTextAutoPhoneNumbertext:RichText=RichText;"));
}

TEST(Phase2InstantViewSchemaContract, RichTextNewTypesPresent) {
  auto tl = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/generate/scheme/td_api.tl"));
  // referenceLink added; reference redefined to name:string text:RichText
  ASSERT_TRUE(has(tl, "richTextReferenceLinktext:RichTextreference_name:stringurl:string=RichText;"));
  ASSERT_TRUE(has(tl, "richTextReferencename:stringtext:RichText=RichText;"));
  // bank card number gained the bank_card_number field
  ASSERT_TRUE(has(tl, "richTextBankCardNumbertext:RichTextbank_card_number:string=RichText;"));
}

}  // namespace
