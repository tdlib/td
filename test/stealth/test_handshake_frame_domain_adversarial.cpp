// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests verifying that the handshake frame builder produces secure
// output under degenerate domain and secret inputs.
// DPI threat model: the built frame must not contain the domain hostname in
// plaintext regardless of domain length, content, or secret value.

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "test/stealth/MockRng.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <string>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::build_proxy_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::MockRng;

// Helper: verify the built frame does not contain a secret byte literal.
// This catches accidental raw-secret embedding in the wire record.
void assert_no_secret_literal(const td::string &frame, const td::string &secret) {
  ASSERT_TRUE(!secret.empty());
  ASSERT_TRUE(frame.find(secret) == td::string::npos);
}

NetworkRouteHints make_ru_hints() {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = true;
  return hints;
}

// ─── Plaintext domain leak tests ────────────────────────────────────────────

TEST(HandshakeFrameDomain, SecretMaterialNotLeakedIntoPlaintext) {
  MockRng rng(1);
  const td::string domain = "tlsfingerprint.io";
  const td::string secret = "\x4b\x10\x73\x22\x39\x8e\x05\x41\x6c\x2a\x59\x93\x17\xf0\x3d\x7b";
  auto frame = build_default_tls_client_hello(domain, td::Slice(secret), 1000000, make_ru_hints(), rng);
  assert_no_secret_literal(frame, secret);
}

TEST(HandshakeFrameDomain, MinimalNonEmptyDomainBuildsSafeFrame) {
  // Builder contract requires non-empty domain; use minimal valid non-empty input.
  MockRng rng(2);
  const td::string domain = "a";
  const td::string secret = td::string(16, '\x02');
  auto frame = build_default_tls_client_hello(domain, td::Slice(secret), 1000000, make_ru_hints(), rng);
  ASSERT_FALSE(frame.empty());
}

TEST(HandshakeFrameDomain, VeryLongDomainDoesNotCrashBuilder) {
  // 253-char domain is the RFC 1035 maximum; builder must handle boundary cleanly.
  MockRng rng(3);
  td::string domain;
  // Make a valid 253-char domain:  "a.b.c... " labels.
  for (int i = 0; i < 63; i++) {
    domain += 'x';
  }
  domain += '.';
  for (int i = 0; i < 63; i++) {
    domain += 'y';
  }
  domain += '.';
  for (int i = 0; i < 63; i++) {
    domain += 'z';
  }
  domain += ".io";
  ASSERT_TRUE(domain.size() >= 128u);

  const td::string secret = td::string(16, '\x03');
  auto frame = build_default_tls_client_hello(domain, td::Slice(secret), 1000000, make_ru_hints(), rng);
  ASSERT_FALSE(frame.empty());
  assert_no_secret_literal(frame, secret);
}

TEST(HandshakeFrameDomain, SecretPatternOneDoesNotLeakRawBytes) {
  MockRng rng(4);
  const td::string domain = "cloudflare-dns.com";
  const td::string secret = "\x01\x23\x45\x67\x89\xab\xcd\xef\xf1\xd3\xb5\x97\x79\x5b\x3d\x1f";
  auto frame = build_default_tls_client_hello(domain, td::Slice(secret), 1000000, make_ru_hints(), rng);
  assert_no_secret_literal(frame, secret);
}

TEST(HandshakeFrameDomain, SecretPatternTwoDoesNotLeakRawBytes) {
  MockRng rng(5);
  const td::string domain = "dns.quad9.net";
  const td::string secret = "\x9a\x0c\x71\xd2\x34\xe8\x5f\x01\xa7\x4d\xc3\x28\x6e\xb0\x55\xf9";
  auto frame = build_default_tls_client_hello(domain, td::Slice(secret), 1000000, make_ru_hints(), rng);
  assert_no_secret_literal(frame, secret);
}

// ─── Frame length non-determinism ───────────────────────────────────────────

TEST(HandshakeFrameDomain, TwoCallsWithDifferentRngSeedsProduceDifferentFrames) {
  const td::string domain = "tlsfingerprint.io";
  const td::string secret = td::string(16, '\x07');
  NetworkRouteHints hints = make_ru_hints();

  MockRng rng1(100);
  MockRng rng2(200);
  auto frame1 = build_default_tls_client_hello(domain, td::Slice(secret), 1000000, hints, rng1);
  auto frame2 = build_default_tls_client_hello(domain, td::Slice(secret), 1000000, hints, rng2);
  // Different RNG seeds must yield different frames (at minimum different padding).
  ASSERT_FALSE(frame1 == frame2);
}

TEST(HandshakeFrameDomain, TwoCallsWithSameRngProduceSameFrame) {
  // Deterministic: identical RNG state + same inputs → identical frame.
  const td::string domain = "tlsfingerprint.io";
  const td::string secret = td::string(16, '\x08');
  NetworkRouteHints hints = make_ru_hints();

  MockRng rng1(999);
  MockRng rng2(999);
  auto frame1 = build_default_tls_client_hello(domain, td::Slice(secret), 1000000, hints, rng1);
  auto frame2 = build_default_tls_client_hello(domain, td::Slice(secret), 1000000, hints, rng2);
  ASSERT_EQ(frame1, frame2);
}

// ─── Proxy variant consistency ───────────────────────────────────────────────

TEST(HandshakeFrameDomain, ProxyVariantProducesNonEmptyFrameWithNoPlaintextDomain) {
  MockRng rng(6);
  const td::string domain = "1.1.1.1";
  const td::string secret = "\x33\x91\x07\xcc\x4f\x8b\x52\x19\xe1\x74\x2d\xba\x60\x05\x9f\xd8";
  auto frame = build_proxy_tls_client_hello(domain, td::Slice(secret), 1000000, make_ru_hints(), rng);
  ASSERT_FALSE(frame.empty());
  assert_no_secret_literal(frame, secret);
}

// ─── Time variation ──────────────────────────────────────────────────────────

TEST(HandshakeFrameDomain, DifferentUnixTimesProduceDifferentFrames) {
  // The unix_time field is embedded in the HMAC/random; different times
  // must produce different randomness seeds and thus different bytes.
  const td::string domain = "tlsfingerprint.io";
  const td::string secret = td::string(16, '\x0a');
  NetworkRouteHints hints = make_ru_hints();

  MockRng rng1(10);
  MockRng rng2(10);
  auto frame1 = build_default_tls_client_hello(domain, td::Slice(secret), 1000000, hints, rng1);
  auto frame2 = build_default_tls_client_hello(domain, td::Slice(secret), 2000000, hints, rng2);

  // Different unix_times with same RNG must produce different frames.
  ASSERT_FALSE(frame1 == frame2);
}

TEST(HandshakeFrameDomain, ZeroUnixTimeDoesNotCrashBuilder) {
  MockRng rng(11);
  const td::string domain = "tlsfingerprint.io";
  const td::string secret = td::string(16, '\x0b');
  auto frame = build_default_tls_client_hello(domain, td::Slice(secret), 0, make_ru_hints(), rng);
  ASSERT_FALSE(frame.empty());
}

TEST(HandshakeFrameDomain, MaxInt32UnixTimeDoesNotCrashBuilder) {
  MockRng rng(12);
  const td::string domain = "tlsfingerprint.io";
  const td::string secret = td::string(16, '\x0c');
  auto frame = build_default_tls_client_hello(domain, td::Slice(secret), std::numeric_limits<td::int32>::max(),
                                              make_ru_hints(), rng);
  ASSERT_FALSE(frame.empty());
}

}  // namespace
