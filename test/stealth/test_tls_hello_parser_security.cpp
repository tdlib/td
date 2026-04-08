// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsHelloWireMutator.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::append_one_byte_to_ech_extension_value;
using td::mtproto::test::duplicate_extension;
using td::mtproto::test::duplicate_key_share_group;
using td::mtproto::test::duplicate_supported_group;
using td::mtproto::test::get_hello_offsets;
using td::mtproto::test::mutate_ech_value_prefix;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::set_ech_declared_enc_length;
using td::mtproto::test::set_ech_payload_length;
using td::mtproto::test::set_extension_type;
using td::mtproto::test::set_padding_first_byte;

td::string build_ech_enabled_client_hello(td::int32 unix_time) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;
  return build_default_tls_client_hello("www.google.com", "0123456789secret", unix_time, hints);
}

TEST(TlsHelloParserSecurity, RejectsUnexpectedRecordAndHandshakeTypes) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string bad_record = wire;
  bad_record[0] = '\x15';
  ASSERT_TRUE(parse_tls_client_hello(bad_record).is_error());

  td::string bad_handshake = wire;
  auto offsets = get_hello_offsets(wire);
  bad_handshake[offsets.handshake_type_offset] = '\x02';
  ASSERT_TRUE(parse_tls_client_hello(bad_handshake).is_error());
}

TEST(TlsHelloParserSecurity, RejectsInvalidCompressionMethods) {
  auto wire = build_ech_enabled_client_hello(1712345678);
  auto offsets = get_hello_offsets(wire);

  td::string tampered = wire;
  tampered[offsets.compression_methods_offset] = '\x01';

  ASSERT_TRUE(parse_tls_client_hello(tampered).is_error());
}

TEST(TlsHelloParserSecurity, RejectsDuplicateCriticalExtensions) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string dup_supported_groups = wire;
  ASSERT_TRUE(duplicate_extension(dup_supported_groups, 0x000A));
  ASSERT_TRUE(parse_tls_client_hello(dup_supported_groups).is_error());

  td::string dup_ech = wire;
  ASSERT_TRUE(duplicate_extension(dup_ech, 0xFE0D));
  ASSERT_TRUE(parse_tls_client_hello(dup_ech).is_error());
}

TEST(TlsHelloParserSecurity, RejectsDuplicateGroupsInsideVectorPayloads) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string dup_supported_group = wire;
  ASSERT_TRUE(duplicate_supported_group(dup_supported_group));
  ASSERT_TRUE(parse_tls_client_hello(dup_supported_group).is_error());

  td::string dup_key_share_group = wire;
  ASSERT_TRUE(duplicate_key_share_group(dup_key_share_group));
  ASSERT_TRUE(parse_tls_client_hello(dup_key_share_group).is_error());
}

TEST(TlsHelloParserSecurity, RejectsMalformedEchSuitePrefixFields) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string bad_outer = wire;
  ASSERT_TRUE(mutate_ech_value_prefix(bad_outer, 0x01, 0x0001, 0x0001));
  ASSERT_TRUE(parse_tls_client_hello(bad_outer).is_error());

  td::string bad_kdf = wire;
  ASSERT_TRUE(mutate_ech_value_prefix(bad_kdf, 0x00, 0x0002, 0x0001));
  ASSERT_TRUE(parse_tls_client_hello(bad_kdf).is_error());

  td::string bad_aead = wire;
  ASSERT_TRUE(mutate_ech_value_prefix(bad_aead, 0x00, 0x0001, 0x0002));
  ASSERT_TRUE(parse_tls_client_hello(bad_aead).is_error());
}

TEST(TlsHelloParserSecurity, RejectsZeroLengthEchEncapsulatedKey) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string bad_enc_len = wire;
  ASSERT_TRUE(set_ech_declared_enc_length(bad_enc_len, 0));
  ASSERT_TRUE(parse_tls_client_hello(bad_enc_len).is_error());
}

TEST(TlsHelloParserSecurity, RejectsZeroLengthEchPayload) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string bad_payload_len = wire;
  ASSERT_TRUE(set_ech_payload_length(bad_payload_len, 0));
  ASSERT_TRUE(parse_tls_client_hello(bad_payload_len).is_error());
}

TEST(TlsHelloParserSecurity, RejectsEchTrailingBytesWhenLengthsAreConsistent) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string bad_trailing = wire;
  ASSERT_TRUE(append_one_byte_to_ech_extension_value(bad_trailing));
  ASSERT_TRUE(parse_tls_client_hello(bad_trailing).is_error());
}

TEST(TlsHelloParserSecurity, RejectsLegacyEchExtensionTypeFe02) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  td::string legacy_ech = wire;
  ASSERT_TRUE(set_extension_type(legacy_ech, 0xFE0D, 0xFE02));
  ASSERT_TRUE(parse_tls_client_hello(legacy_ech).is_error());
}

TEST(TlsHelloParserSecurity, RejectsNonZeroPaddingBytes) {
  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  bool mutated = false;
  for (int i = 0; i < 256; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i, hints);
    td::string non_zero_padding = wire;
    if (!set_padding_first_byte(non_zero_padding, 0x01)) {
      continue;
    }
    mutated = true;
    ASSERT_TRUE(parse_tls_client_hello(non_zero_padding).is_error());
    break;
  }

  ASSERT_TRUE(mutated);
}

}  // namespace
