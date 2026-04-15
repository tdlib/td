// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Contract tests for `parse_tls_server_hello`.
//
// Covers: a valid TLS 1.3 ServerHello, a valid TLS 1.2 ServerHello that
// does NOT carry a supported_versions extension, a ServerHello whose
// random matches the HelloRetryRequest sentinel, a truncated wire,
// a wrong record type and an unexpected handshake type. Each case
// verifies a single observable of `parse_tls_server_hello`.

#include "test/stealth/ServerHelloFixtureLoader.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/Slice.h"
#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <cstring>

namespace {

using td::mtproto::test::append_u16;
using td::mtproto::test::append_u24;
using td::mtproto::test::append_u8;
using td::mtproto::test::kHelloRetryRequestRandom;
using td::mtproto::test::parse_tls_server_hello;
using td::mtproto::test::ServerHelloFixtureSample;
using td::mtproto::test::synthesize_server_hello_wire;

td::string build_minimal_server_hello(td::uint16 selected_version, bool use_hrr_random,
                                      td::uint8 record_type_override = 0x16,
                                      td::uint8 handshake_type_override = 0x02,
                                      bool include_supported_versions_ext = true) {
  td::string body;
  append_u16(body, 0x0303);  // server_legacy_version
  if (use_hrr_random) {
    for (size_t i = 0; i < kHelloRetryRequestRandom.size(); ++i) {
      append_u8(body, kHelloRetryRequestRandom[i]);
    }
  } else {
    for (int i = 0; i < 32; ++i) {
      append_u8(body, static_cast<td::uint8>(0x10 + (i & 0x0F)));
    }
  }
  append_u8(body, 0);         // empty session_id
  append_u16(body, 0x1301);   // cipher_suite
  append_u8(body, 0x00);      // compression_method

  td::string extensions;
  if (include_supported_versions_ext) {
    append_u16(extensions, 0x002B);
    append_u16(extensions, 2);
    append_u16(extensions, selected_version);
  }
  // Include key_share with X25519
  append_u16(extensions, 0x0033);
  append_u16(extensions, 36);
  append_u16(extensions, 0x001D);
  append_u16(extensions, 32);
  for (int i = 0; i < 32; ++i) {
    append_u8(extensions, static_cast<td::uint8>(0x20 + (i & 0x0F)));
  }

  append_u16(body, static_cast<td::uint16>(extensions.size()));
  body.append(extensions);

  td::string handshake;
  append_u8(handshake, handshake_type_override);
  append_u24(handshake, static_cast<td::uint32>(body.size()));
  handshake.append(body);

  td::string wire;
  append_u8(wire, record_type_override);
  append_u16(wire, 0x0303);
  append_u16(wire, static_cast<td::uint16>(handshake.size()));
  wire.append(handshake);
  return wire;
}

TEST(TlsServerHelloParserContract, ValidTls13ServerHelloParsesOk) {
  auto wire = build_minimal_server_hello(0x0304, /*use_hrr_random=*/false);
  auto parsed = parse_tls_server_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  auto &sh = parsed.ok_ref();
  ASSERT_EQ(static_cast<td::uint8>(0x16), sh.record_type);
  ASSERT_EQ(static_cast<td::uint8>(0x02), sh.handshake_type);
  ASSERT_EQ(static_cast<td::uint16>(0x0303), sh.server_legacy_version);
  ASSERT_EQ(static_cast<td::uint16>(0x1301), sh.cipher_suite);
  ASSERT_EQ(static_cast<td::uint8>(0x00), sh.compression_method);
  ASSERT_EQ(static_cast<td::uint16>(0x0304), sh.supported_version_extension_value);
  ASSERT_FALSE(sh.is_hello_retry_request);
  ASSERT_EQ(static_cast<td::uint16>(0x001D), sh.key_share_group);
  ASSERT_EQ(static_cast<size_t>(32), sh.key_share_public_key.size());
}

TEST(TlsServerHelloParserContract, ValidTls12ServerHelloParsesOkWithoutSupportedVersions) {
  auto wire = build_minimal_server_hello(/*selected_version=*/0, /*use_hrr_random=*/false,
                                         /*record_type_override=*/0x16, /*handshake_type_override=*/0x02,
                                         /*include_supported_versions_ext=*/false);
  auto parsed = parse_tls_server_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  auto &sh = parsed.ok_ref();
  ASSERT_EQ(static_cast<td::uint16>(0), sh.supported_version_extension_value);
}

TEST(TlsServerHelloParserContract, HrrSentinelRandomIsRecognized) {
  auto wire = build_minimal_server_hello(0x0304, /*use_hrr_random=*/true);
  auto parsed = parse_tls_server_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(parsed.ok_ref().is_hello_retry_request);
}

TEST(TlsServerHelloParserContract, TruncatedWireFailsToParse) {
  auto wire = build_minimal_server_hello(0x0304, /*use_hrr_random=*/false);
  // Drop the last 8 bytes: record length header will no longer match.
  wire.resize(wire.size() - 8);
  auto parsed = parse_tls_server_hello(wire);
  ASSERT_TRUE(parsed.is_error());
}

TEST(TlsServerHelloParserContract, WrongRecordTypeFailsToParse) {
  auto wire = build_minimal_server_hello(0x0304, /*use_hrr_random=*/false,
                                         /*record_type_override=*/0x17);  // application_data
  auto parsed = parse_tls_server_hello(wire);
  ASSERT_TRUE(parsed.is_error());
}

TEST(TlsServerHelloParserContract, UnexpectedHandshakeTypeFailsToParse) {
  auto wire = build_minimal_server_hello(0x0304, /*use_hrr_random=*/false,
                                         /*record_type_override=*/0x16,
                                         /*handshake_type_override=*/0x01);  // ClientHello
  auto parsed = parse_tls_server_hello(wire);
  ASSERT_TRUE(parsed.is_error());
}

TEST(TlsServerHelloParserContract, SynthesizedFromFixtureMetadataParsesOk) {
  ServerHelloFixtureSample sample;
  sample.cipher_suite = 0x1301;
  sample.selected_version = 0x0304;
  sample.extension_types = {0x002B, 0x0033};
  auto wire = synthesize_server_hello_wire(sample);
  auto parsed = parse_tls_server_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_EQ(static_cast<td::uint16>(0x0304), parsed.ok_ref().supported_version_extension_value);
}

}  // namespace
