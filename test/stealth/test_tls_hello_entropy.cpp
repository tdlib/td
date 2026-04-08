// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::parse_tls_client_hello;

td::string build_ech_enabled_client_hello(td::int32 unix_time) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;
  return build_default_tls_client_hello("www.google.com", "0123456789secret", unix_time, hints);
}

TEST(TlsHelloEntropy, EchPayloadLengthAllowsetAndVariance) {
  std::unordered_set<td::uint16> observed_lengths;
  for (int i = 0; i < 128; i++) {
    auto wire = build_ech_enabled_client_hello(1712345678 + i);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    auto ech_payload_length = parsed.ok().ech_payload_length;
    ASSERT_TRUE(ech_payload_length == 144 || ech_payload_length == 176 || ech_payload_length == 208 ||
                ech_payload_length == 240);
    observed_lengths.insert(ech_payload_length);
  }

  // ECH payload entropy must not collapse to a process-wide singleton.
  ASSERT_TRUE(observed_lengths.size() > 1u);
}

TEST(TlsHelloEntropy, EchEncapsulatedKeyLengthMustMatch32Bytes) {
  auto wire = build_ech_enabled_client_hello(1712345678);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_EQ(32u, parsed.ok().ech_declared_enc_length);
  ASSERT_EQ(32u, parsed.ok().ech_actual_enc_length);
}

TEST(TlsHelloEntropy, ClientHelloLengthMustNotBePinnedToFixed517) {
  std::unordered_set<size_t> lengths;
  for (int i = 0; i < 128; i++) {
    auto wire = build_ech_enabled_client_hello(1712345678 + i);
    lengths.insert(wire.size());
  }

  // 517 can appear in some profiles, but a single fixed wire length is fingerprintable.
  ASSERT_TRUE(lengths.size() > 1u);
}

}  // namespace
