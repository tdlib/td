// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: plaintext sensitive-name leak via ECH-disabled and
// QUIC-blocked fallback paths (RISK-FP-29).
//
// When ECH is disabled (RU route, unknown route, circuit-breaker trip) or
// when QUIC is blocked and the transport falls back, the generated
// ClientHello and any probe packets must NOT leak a sensitive target
// hostname in plaintext SNI or in any other identifiable field.
//
// These tests cover five attack surfaces:
//   1. RU route with ECH disabled: SNI must NOT contain a sensitive hostname
//   2. Unknown route: same check
//   3. Error/timeout path: no unique probe hostname leaked
//   4. Fallback from blocked transport: no distinctive retry signature
//   5. QUIC-blocked lane: no QUIC-looking probe packets
//
// All tests are RED by design: they assert contracts for route-aware SNI
// masking and transport-fallback opacity that are not yet implemented.
// The tests compile and run but are expected to fail until the
// corresponding stealth features land.

#include "test/stealth/MockRng.h"
#include "test/stealth/ProxySecretSniTestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

namespace {

using td::mtproto::stealth::all_profiles;
using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::build_proxy_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_single_sni_hostname;
using td::mtproto::test::parse_tls_client_hello;

using td::mtproto::test::fixtures::kEchExtensionType;

// Extension type constants
constexpr td::uint16 kSniExtensionType = 0x0000;

// -----------------------------------------------------------------------
// Sensitive hostnames that must never appear in plaintext SNI when ECH is
// disabled. A DPI adversary pattern-matching on these would immediately
// identify Telegram traffic regardless of fingerprint quality.
// -----------------------------------------------------------------------
static const char *const kSensitiveHostnames[] = {
    "core.telegram.org",
    "api.telegram.org",
    "venus.web.telegram.org",
    "pluto.web.telegram.org",
    "flora.web.telegram.org",
};

// A benign cover hostname that real browser traffic would plausibly visit.
static constexpr const char *kCoverDomain = "www.google.com";

// -----------------------------------------------------------------------
// Helper: extract the plaintext SNI hostname from a parsed ClientHello.
// Returns an empty string if the SNI extension is absent.
// -----------------------------------------------------------------------
static td::string extract_sni_hostname(const td::mtproto::test::ParsedClientHello &hello) {
  auto *sni_ext = find_extension(hello, kSniExtensionType);
  if (sni_ext == nullptr) {
    return td::string();
  }
  auto r_hostname = parse_single_sni_hostname(sni_ext->value);
  if (r_hostname.is_error()) {
    return td::string();
  }
  return r_hostname.move_as_ok();
}

// -----------------------------------------------------------------------
// Helper: check whether a sensitive hostname appears anywhere in the raw
// wire bytes of a ClientHello. This is a stronger check than just looking
// at the SNI extension -- it catches accidental leaks in other fields
// (e.g., ALPN, padding, ECH payload when ECH is supposed to be disabled).
// -----------------------------------------------------------------------
static bool wire_contains_hostname(td::Slice wire, td::Slice hostname) {
  if (hostname.empty() || wire.size() < hostname.size()) {
    return false;
  }
  // Brute-force substring search over the raw wire bytes.
  for (size_t i = 0; i <= wire.size() - hostname.size(); i++) {
    if (std::memcmp(wire.data() + i, hostname.data(), hostname.size()) == 0) {
      return true;
    }
  }
  return false;
}

// =======================================================================
// TEST 1: RU route with ECH disabled -- generated ClientHello must NOT
//         contain a plaintext sensitive hostname in the SNI extension.
//
// Contract: when the route is RU (ECH is always disabled for RU), the
// builder must replace any sensitive destination hostname with a benign
// cover name in the outer SNI. The current implementation passes the
// destination domain through to SNI unchanged, so this test is RED.
// =======================================================================

TEST(TlsPlaintextNameLeakAdversarial, RuRouteEchDisabledSniMustNotLeakSensitiveHostname) {
  NetworkRouteHints ru_hints;
  ru_hints.is_known = true;
  ru_hints.is_ru = true;

  td::Slice secret("0123456789secret");

  for (const char *sensitive_host : kSensitiveHostnames) {
    for (td::uint64 seed = 0; seed < 20; seed++) {
      MockRng rng(seed);
      auto wire = build_default_tls_client_hello(sensitive_host, secret, 1712345678, ru_hints, rng);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());

      // ECH must be absent on an RU route.
      ASSERT_TRUE(find_extension(parsed.ok(), kEchExtensionType) == nullptr);

      // The SNI extension must be present (omitting it entirely would be
      // its own fingerprint) but must NOT contain the sensitive hostname.
      auto sni_hostname = extract_sni_hostname(parsed.ok());
      ASSERT_TRUE(!sni_hostname.empty());

      // RED assertion: the sensitive hostname must not appear in the SNI.
      // This will fail until route-aware SNI masking is implemented.
      ASSERT_TRUE(sni_hostname != td::string(sensitive_host));
    }
  }
}

TEST(TlsPlaintextNameLeakAdversarial, RuRouteAllProfilesSniMustNotLeakSensitiveHostname) {
  for (auto profile : all_profiles()) {
    for (const char *sensitive_host : kSensitiveHostnames) {
      MockRng rng(42);
      auto wire = build_proxy_tls_client_hello_for_profile(sensitive_host, "0123456789secret", 1712345678, profile,
                                                           EchMode::Disabled, rng);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());

      // ECH must be absent.
      ASSERT_TRUE(find_extension(parsed.ok(), kEchExtensionType) == nullptr);

      auto sni_hostname = extract_sni_hostname(parsed.ok());
      ASSERT_TRUE(!sni_hostname.empty());

      // RED: sensitive hostname must not appear in plaintext SNI.
      ASSERT_TRUE(sni_hostname != td::string(sensitive_host));
    }
  }
}

// =======================================================================
// TEST 2: Unknown route -- same plaintext SNI check.
//
// When the route is unknown (country code unavailable or unresolvable),
// ECH is disabled as a conservative default. The same SNI masking
// contract applies.
// =======================================================================

TEST(TlsPlaintextNameLeakAdversarial, UnknownRouteEchDisabledSniMustNotLeakSensitiveHostname) {
  NetworkRouteHints unknown_hints;
  unknown_hints.is_known = false;
  unknown_hints.is_ru = false;

  td::Slice secret("0123456789secret");

  for (const char *sensitive_host : kSensitiveHostnames) {
    for (td::uint64 seed = 0; seed < 20; seed++) {
      MockRng rng(seed);
      auto wire = build_default_tls_client_hello(sensitive_host, secret, 1712345678, unknown_hints, rng);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());

      // ECH must be absent on an unknown route.
      ASSERT_TRUE(find_extension(parsed.ok(), kEchExtensionType) == nullptr);

      auto sni_hostname = extract_sni_hostname(parsed.ok());
      ASSERT_TRUE(!sni_hostname.empty());

      // RED: sensitive hostname must not appear in plaintext SNI.
      ASSERT_TRUE(sni_hostname != td::string(sensitive_host));
    }
  }
}

TEST(TlsPlaintextNameLeakAdversarial, UnknownRouteNoSensitiveHostnameInRawWireBytes) {
  NetworkRouteHints unknown_hints;
  unknown_hints.is_known = false;
  unknown_hints.is_ru = false;

  td::Slice secret("0123456789secret");

  for (const char *sensitive_host : kSensitiveHostnames) {
    MockRng rng(42);
    auto wire = build_default_tls_client_hello(sensitive_host, secret, 1712345678, unknown_hints, rng);

    // RED: the sensitive hostname must not appear anywhere in the raw wire
    // bytes -- not just in the SNI extension. This catches leaks through
    // misconfigured ECH payloads, ALPN, or padding fields.
    ASSERT_FALSE(wire_contains_hostname(wire, sensitive_host));
  }
}

// =======================================================================
// TEST 3: Error/timeout path -- no unique probe hostname leaked.
//
// When connections fail and the library performs probe/retry attempts, the
// retry ClientHello must not contain a unique hostname that an adversary
// could use to identify the probe as distinct from normal traffic. The
// probe must reuse the same benign cover domain or the previously
// established SNI, not inject a distinguishable test hostname.
//
// We simulate this by generating multiple ClientHellos with different
// timestamps (simulating retries) and checking that the SNI hostnames
// across retries do not form a distinguishable set.
// =======================================================================

TEST(TlsPlaintextNameLeakAdversarial, RetryConnectionsSniMustNotDiverge) {
  NetworkRouteHints ru_hints;
  ru_hints.is_known = true;
  ru_hints.is_ru = true;

  td::Slice secret("0123456789secret");

  // Simulate a burst of retry attempts with slightly different timestamps.
  std::set<td::string> observed_sni_hostnames;
  for (int retry = 0; retry < 50; retry++) {
    MockRng rng(static_cast<td::uint64>(retry));
    auto wire = build_default_tls_client_hello(kCoverDomain, secret, 1712345678 + retry, ru_hints, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    auto sni_hostname = extract_sni_hostname(parsed.ok());
    ASSERT_TRUE(!sni_hostname.empty());
    observed_sni_hostnames.insert(sni_hostname);
  }

  // All retries must use the same cover domain -- a varying SNI across
  // retries (e.g., probe-1.example.com, probe-2.example.com) would let
  // a passive adversary identify the retry pattern.
  ASSERT_EQ(1u, observed_sni_hostnames.size());
  ASSERT_TRUE(observed_sni_hostnames.count(kCoverDomain) == 1u);
}

TEST(TlsPlaintextNameLeakAdversarial, ErrorPathSniNeverContainsSensitiveHostAcrossProfiles) {
  // Even when retrying across different profiles (which could happen if
  // the runtime re-selects a profile on failure), the SNI must remain
  // consistent and benign.
  std::set<td::string> observed_sni_hostnames;

  for (auto profile : all_profiles()) {
    for (int retry = 0; retry < 10; retry++) {
      MockRng rng(static_cast<td::uint64>(retry) + 1000);
      auto wire = build_proxy_tls_client_hello_for_profile(kCoverDomain, "0123456789secret", 1712345678 + retry,
                                                           profile, EchMode::Disabled, rng);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());

      auto sni_hostname = extract_sni_hostname(parsed.ok());
      ASSERT_TRUE(!sni_hostname.empty());
      observed_sni_hostnames.insert(sni_hostname);
    }
  }

  // All profiles and retries must converge on the same cover domain.
  ASSERT_EQ(1u, observed_sni_hostnames.size());
  ASSERT_TRUE(observed_sni_hostnames.count(kCoverDomain) == 1u);
}

// =======================================================================
// TEST 4: Fallback from blocked transport -- no distinctive retry
//         signature.
//
// When a transport is blocked and the client falls back (e.g., from a
// direct connection to a proxy, or from one DC to another), the fallback
// ClientHello must be statistically indistinguishable from a fresh
// connection. The wire image must not carry any "I am a retry" signal
// that a DPI adversary could match across connection attempts.
//
// We check this by generating ClientHellos for "first attempt" and
// "fallback attempt" scenarios and verifying key structural properties
// are statistically equivalent.
// =======================================================================

TEST(TlsPlaintextNameLeakAdversarial, FallbackWireImageNotDistinctFromFreshConnection) {
  NetworkRouteHints ru_hints;
  ru_hints.is_known = true;
  ru_hints.is_ru = true;

  td::Slice secret("0123456789secret");

  // Collect wire length distributions for "first" and "retry" connection
  // attempts. The seeds differ but the domain and route are the same.
  std::vector<size_t> first_attempt_lengths;
  std::vector<size_t> retry_attempt_lengths;

  for (int i = 0; i < 100; i++) {
    MockRng rng_first(static_cast<td::uint64>(i));
    auto wire_first = build_default_tls_client_hello(kCoverDomain, secret, 1712345678, ru_hints, rng_first);
    first_attempt_lengths.push_back(wire_first.size());

    MockRng rng_retry(static_cast<td::uint64>(i) + 50000);
    auto wire_retry = build_default_tls_client_hello(kCoverDomain, secret, 1712345678 + 30, ru_hints, rng_retry);
    retry_attempt_lengths.push_back(wire_retry.size());
  }

  // The length distributions must overlap. A blocked-transport fallback
  // that systematically produces longer or shorter ClientHellos would be
  // trivially distinguishable.
  std::sort(first_attempt_lengths.begin(), first_attempt_lengths.end());
  std::sort(retry_attempt_lengths.begin(), retry_attempt_lengths.end());

  auto first_median = first_attempt_lengths[first_attempt_lengths.size() / 2];
  auto retry_median = retry_attempt_lengths[retry_attempt_lengths.size() / 2];

  // Median wire lengths must be within 5% of each other. A larger
  // divergence would indicate the retry path injects extra data (e.g.,
  // a retry indicator extension, different padding strategy).
  if (first_median > 0 && retry_median > 0) {
    auto diff = (first_median > retry_median) ? (first_median - retry_median) : (retry_median - first_median);
    auto tolerance = first_median / 20;  // 5%
    ASSERT_TRUE(diff <= tolerance);
  }
}

TEST(TlsPlaintextNameLeakAdversarial, FallbackExtensionSetIdenticalToFreshConnection) {
  NetworkRouteHints ru_hints;
  ru_hints.is_known = true;
  ru_hints.is_ru = true;

  td::Slice secret("0123456789secret");

  // Generate a "first attempt" and a "retry attempt" and verify that the
  // set of non-GREASE extension types is identical.
  auto extract_non_grease_extension_types = [](const td::mtproto::test::ParsedClientHello &hello) {
    std::set<td::uint16> types;
    for (const auto &ext : hello.extensions) {
      auto hi = static_cast<td::uint8>((ext.type >> 8) & 0xFF);
      auto lo = static_cast<td::uint8>(ext.type & 0xFF);
      bool is_grease = (hi == lo && (hi & 0x0F) == 0x0A);
      if (!is_grease && ext.type != 0x0015 /* padding */) {
        types.insert(ext.type);
      }
    }
    return types;
  };

  for (int i = 0; i < 50; i++) {
    MockRng rng_first(static_cast<td::uint64>(i));
    auto wire_first = build_default_tls_client_hello(kCoverDomain, secret, 1712345678, ru_hints, rng_first);
    auto parsed_first = parse_tls_client_hello(wire_first);
    ASSERT_TRUE(parsed_first.is_ok());

    MockRng rng_retry(static_cast<td::uint64>(i) + 50000);
    auto wire_retry = build_default_tls_client_hello(kCoverDomain, secret, 1712345678 + 60, ru_hints, rng_retry);
    auto parsed_retry = parse_tls_client_hello(wire_retry);
    ASSERT_TRUE(parsed_retry.is_ok());

    auto first_ext_types = extract_non_grease_extension_types(parsed_first.ok());
    auto retry_ext_types = extract_non_grease_extension_types(parsed_retry.ok());

    // The extension type sets must be identical -- a retry that adds or
    // removes extensions is trivially distinguishable by DPI.
    ASSERT_TRUE(first_ext_types == retry_ext_types);
  }
}

// =======================================================================
// TEST 5: QUIC-blocked lane -- no QUIC-looking probe packets.
//
// When QUIC is blocked, the library must not emit any UDP-based probe
// packets that look like QUIC Initial packets. A DPI adversary can
// trivially identify QUIC Initial packets by the first byte pattern
// (0xC0..0xFF for Long Header) and the fixed Version field.
//
// Since the current codebase does not implement QUIC at all, this test
// validates the invariant at the TLS layer: the generated wire bytes
// must always be a valid TLS record (starting with 0x16 for Handshake)
// and must never start with a QUIC Long Header byte pattern.
//
// Additionally, across many seeds, no generated wire image should start
// with the QUIC Initial packet pattern to ensure no code path
// accidentally emits QUIC-like data.
// =======================================================================

TEST(TlsPlaintextNameLeakAdversarial, NoQuicLookingProbePacketsOnTlsPath) {
  // QUIC Long Header form: first bit is 1, second bit is 1 => first byte >= 0xC0
  // QUIC version fields for known versions:
  constexpr td::uint32 kQuicVersion1 = 0x00000001;
  constexpr td::uint32 kQuicVersion2 = 0x6B3343CF;

  NetworkRouteHints ru_hints;
  ru_hints.is_known = true;
  ru_hints.is_ru = true;

  td::Slice secret("0123456789secret");

  for (auto profile : all_profiles()) {
    for (td::uint64 seed = 0; seed < 50; seed++) {
      MockRng rng(seed);
      auto wire = build_proxy_tls_client_hello_for_profile(kCoverDomain, "0123456789secret", 1712345678, profile,
                                                           EchMode::Disabled, rng);

      // The wire must be a valid TLS record.
      ASSERT_TRUE(wire.size() >= 5u);

      // First byte must be TLS ContentType::Handshake (0x16), NOT a QUIC
      // Long Header form byte (>= 0xC0).
      auto first_byte = static_cast<td::uint8>(wire[0]);
      ASSERT_EQ(static_cast<td::uint8>(0x16), first_byte);
      ASSERT_TRUE(first_byte < 0xC0u);

      // Additionally, the wire must parse as a valid TLS ClientHello.
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());

      // Check that no 4-byte subsequence at offset 1..4 matches a known
      // QUIC version field -- this would indicate contamination from a
      // QUIC code path writing into TLS buffers.
      if (wire.size() >= 5) {
        td::uint32 bytes_1_4 = (static_cast<td::uint32>(static_cast<td::uint8>(wire[1])) << 24) |
                               (static_cast<td::uint32>(static_cast<td::uint8>(wire[2])) << 16) |
                               (static_cast<td::uint32>(static_cast<td::uint8>(wire[3])) << 8) |
                               static_cast<td::uint32>(static_cast<td::uint8>(wire[4]));
        ASSERT_TRUE(bytes_1_4 != kQuicVersion1);
        ASSERT_TRUE(bytes_1_4 != kQuicVersion2);
      }
    }
  }
}

TEST(TlsPlaintextNameLeakAdversarial, DefaultBuilderNeverEmitsQuicLongHeader) {
  // Stress test: across many seeds and timestamps, the default builder
  // must never produce output that starts with a QUIC Long Header byte.
  NetworkRouteHints hints_variants[] = {
      {true, true},    // RU
      {true, false},   // known non-RU
      {false, false},  // unknown
  };

  td::Slice secret("0123456789secret");

  for (const auto &hints : hints_variants) {
    for (int i = 0; i < 200; i++) {
      MockRng rng(static_cast<td::uint64>(i));
      auto wire = build_default_tls_client_hello(kCoverDomain, secret, 1712345678 + i, hints, rng);

      ASSERT_TRUE(wire.size() >= 5u);
      auto first_byte = static_cast<td::uint8>(wire[0]);

      // Must be TLS Handshake content type.
      ASSERT_EQ(static_cast<td::uint8>(0x16), first_byte);

      // Must NOT look like a QUIC Initial (Long Header bit set).
      ASSERT_TRUE((first_byte & 0x80u) == 0u);
    }
  }
}

// =======================================================================
// Additional coverage: sensitive hostname must not appear in raw wire
// bytes for RU route across all profiles.
// =======================================================================

TEST(TlsPlaintextNameLeakAdversarial, RuRouteSensitiveHostnameNeverInRawWireAcrossAllProfiles) {
  for (auto profile : all_profiles()) {
    for (const char *sensitive_host : kSensitiveHostnames) {
      for (td::uint64 seed : {0u, 42u, 99u, 255u}) {
        MockRng rng(seed);
        auto wire = build_proxy_tls_client_hello_for_profile(sensitive_host, "0123456789secret", 1712345678, profile,
                                                             EchMode::Disabled, rng);

        // RED: the sensitive hostname must not appear anywhere in the raw
        // wire bytes when ECH is disabled on an RU route.
        ASSERT_FALSE(wire_contains_hostname(wire, sensitive_host));
      }
    }
  }
}

// =======================================================================
// Adversarial: multiple sensitive destinations must not produce
// distinguishable wire images when ECH is disabled.
//
// If two connections to different sensitive destinations produce
// ClientHellos that differ only in the SNI field (which contains the
// plaintext sensitive hostname), an adversary performing differential
// analysis across connections can trivially correlate them. The cover
// domain must mask the real destination.
// =======================================================================

TEST(TlsPlaintextNameLeakAdversarial, DifferentSensitiveDestinationsMustNotBeDifferentiableBySni) {
  NetworkRouteHints ru_hints;
  ru_hints.is_known = true;
  ru_hints.is_ru = true;

  td::Slice secret("0123456789secret");

  // Generate ClientHellos for different sensitive hostnames with the same
  // seed and timestamp, then collect the SNI hostnames.
  std::set<td::string> observed_sni_hostnames;
  for (const char *sensitive_host : kSensitiveHostnames) {
    MockRng rng(42);
    auto wire = build_default_tls_client_hello(sensitive_host, secret, 1712345678, ru_hints, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    auto sni_hostname = extract_sni_hostname(parsed.ok());
    ASSERT_TRUE(!sni_hostname.empty());
    observed_sni_hostnames.insert(sni_hostname);
  }

  // RED: all sensitive destinations must produce the same cover hostname
  // in SNI. If each destination gets its own plaintext SNI, the adversary
  // can trivially distinguish them.
  ASSERT_EQ(1u, observed_sni_hostnames.size());

  // The single observed SNI must not be any of the sensitive hostnames.
  for (const char *sensitive_host : kSensitiveHostnames) {
    ASSERT_TRUE(observed_sni_hostnames.count(sensitive_host) == 0u);
  }
}

}  // namespace
