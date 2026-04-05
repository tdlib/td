//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::test::find_extension;
using td::mtproto::test::parse_tls_client_hello;

TEST(TlsHelloWire, StructuralInvariantsAndECHLengths) {
  auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678);

  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  auto hello = parsed.move_as_ok();

  ASSERT_EQ(0x16, hello.record_type);
  ASSERT_EQ(0x01, hello.handshake_type);
  ASSERT_EQ(0x0303, hello.client_legacy_version);

  const auto *ech = find_extension(hello, td::mtproto::test::fixtures::kEchExtensionType);
  ASSERT_TRUE(ech != nullptr);
  ASSERT_EQ(hello.ech_declared_enc_length, hello.ech_actual_enc_length);

  std::unordered_set<td::uint16> supported_groups(hello.supported_groups.begin(), hello.supported_groups.end());
  ASSERT_FALSE(supported_groups.empty());
  ASSERT_FALSE(hello.key_share_groups.empty());
  for (auto group : hello.key_share_groups) {
    ASSERT_TRUE(supported_groups.count(group) != 0);
  }
}

TEST(TlsHelloWire, AdversarialLengthMismatchesMustBeRejected) {
  auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678);

  td::string tampered = wire;
  ASSERT_TRUE(tampered.size() >= 5u);
  tampered[4] = static_cast<char>(tampered[4] ^ 0x01);

  auto parsed = parse_tls_client_hello(tampered);
  ASSERT_TRUE(parsed.is_error());
}

TEST(TlsHelloWire, PaddingMustNotCollapseToSingleFixedLength) {
  std::unordered_set<size_t> lengths;
  for (int i = 0; i < 64; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i);
    lengths.insert(wire.size());
  }

  // Security requirement: avoid a single deterministic wire length across sessions.
  ASSERT_TRUE(lengths.size() > 1u);
}

TEST(TlsHelloWire, EchPayloadLengthMustVaryPerConnection) {
  std::unordered_set<td::uint16> ech_payload_lengths;

  for (int i = 0; i < 64; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ech_payload_lengths.insert(parsed.ok().ech_payload_length);
  }

  // Security requirement: ECH payload entropy must not be process-wide singleton.
  ASSERT_TRUE(ech_payload_lengths.size() > 1u);
}

}  // namespace
