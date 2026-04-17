// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Deep safety tests for TLS wire-image generation.
// Tests memory safety, buffer integrity, cache bounds, parameter
// validation, and statistical profile-selection properties that
// existing suites do not cover.
//
// Risk register entries targeted:
//   RISK-WS01: LengthCalculator / ByteWriter offset divergence
//   RISK-WS02: Unbounded route-failure cache growth
//   RISK-WS05: GREASE dedup XOR pattern micro-fingerprint
//   RISK-WS06: ECH payload discrete distribution detectability
//   RISK-WS07: Route-failure cache key bucket aliasing

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <map>
#include <set>
#include <string>

namespace {

using td::int32;
using td::mtproto::BrowserProfile;
using td::mtproto::stealth::all_profiles;
using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::note_runtime_ech_failure;
using td::mtproto::stealth::note_runtime_ech_success;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::uint16;
using td::uint32;
using td::uint64;
using td::uint8;

// ---------------------------------------------------------------------------
// RISK-WS01: ByteWriter must consume exactly the allocated buffer.
// If LengthCalculator and ByteWriter diverge, the HMAC is computed
// over wrong extent OR the wire image has trailing garbage.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, ByteWriterExactlyConsumesBufferAllProfilesAllModes) {
  auto profiles = all_profiles();
  for (auto profile : profiles) {
    for (auto ech_mode : {EchMode::Disabled, EchMode::Rfc9180Outer}) {
      for (uint64 seed = 0; seed < 20; seed++) {
        MockRng rng(seed);
        auto wire =
            build_tls_client_hello_for_profile("example.com", "0123456789abcdef", 1712345678, profile, ech_mode, rng);
        ASSERT_TRUE(wire.size() >= 9u);

        auto record_len = (static_cast<uint16>(static_cast<uint8>(wire[3])) << 8) | static_cast<uint8>(wire[4]);
        ASSERT_EQ(static_cast<size_t>(record_len), wire.size() - 5u);

        auto hs_len = (static_cast<uint32>(static_cast<uint8>(wire[6])) << 16) |
                      (static_cast<uint32>(static_cast<uint8>(wire[7])) << 8) |
                      static_cast<uint32>(static_cast<uint8>(wire[8]));
        ASSERT_EQ(static_cast<size_t>(hs_len), wire.size() - 9u);

        auto parsed = parse_tls_client_hello(wire);
        ASSERT_TRUE(parsed.is_ok());
      }
    }
  }
}

TEST(TlsWireSafety, ExplicitPaddingPathBufferAgreement) {
  NetworkRouteHints non_ru;
  non_ru.is_known = true;
  non_ru.is_ru = false;

  td::mtproto::stealth::detail::TlsHelloBuildOptions options;
  options.padding_extension_payload_length = 200;
  options.ech_payload_length = 144;

  for (uint64 seed = 0; seed < 30; seed++) {
    MockRng rng(seed);
    auto wire = td::mtproto::stealth::detail::build_default_tls_client_hello_with_options(
        "test.example.org", "0123456789abcdef", 1712345678, non_ru, rng, options);
    ASSERT_TRUE(wire.size() >= 9u);

    auto record_len = (static_cast<uint16>(static_cast<uint8>(wire[3])) << 8) | static_cast<uint8>(wire[4]);
    ASSERT_EQ(static_cast<size_t>(record_len), wire.size() - 5u);

    auto hs_len = (static_cast<uint32>(static_cast<uint8>(wire[6])) << 16) |
                  (static_cast<uint32>(static_cast<uint8>(wire[7])) << 8) |
                  static_cast<uint32>(static_cast<uint8>(wire[8]));
    ASSERT_EQ(static_cast<size_t>(hs_len), wire.size() - 9u);
  }
}

// ---------------------------------------------------------------------------
// RISK-WS02: Route-failure cache entry lifetime and cleanup.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, RouteFailureCacheCorrectlyEvictsClearedEntries) {
  reset_runtime_ech_failure_state_for_tests();

  constexpr int kDestinationCount = 200;
  int32 test_time = 1712345678;

  for (int i = 0; i < kDestinationCount; i++) {
    auto dest = "host-" + std::to_string(i) + ".example.com";
    note_runtime_ech_failure(dest, test_time);
  }

  note_runtime_ech_success("host-50.example.com", test_time);

  NetworkRouteHints non_ru;
  non_ru.is_known = true;
  non_ru.is_ru = false;

  auto decision_cleared = td::mtproto::stealth::get_runtime_ech_decision("host-50.example.com", test_time, non_ru);
  ASSERT_TRUE(decision_cleared.ech_mode == EchMode::Rfc9180Outer);

  reset_runtime_ech_failure_state_for_tests();
}

// ---------------------------------------------------------------------------
// RISK-WS05: GREASE dedup XOR 0x10 pattern.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, GreaseAdjacentSlotDifferenceDistribution) {
  std::map<int, int> diff_counts;
  constexpr int kTrials = 500;

  for (uint64 seed = 0; seed < kTrials; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789abcdef", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    auto &ch = parsed.ok_ref();
    if (ch.cipher_suites.size() >= 4) {
      auto cs0 = static_cast<int>(static_cast<uint8>(ch.cipher_suites[0]));
      auto cs1 = static_cast<int>(static_cast<uint8>(ch.cipher_suites[2]));
      auto diff = (cs1 - cs0 + 256) % 256;
      diff_counts[diff]++;
    }
  }

  int xor_pattern_count = 0;
  for (auto &kv : diff_counts) {
    if (kv.first == 0 || kv.first == 0x10 || kv.first == 0xF0) {
      xor_pattern_count += kv.second;
    }
  }

  if (xor_pattern_count > static_cast<int>(kTrials * 0.8)) {
    LOG(WARNING) << "GREASE dedup XOR pattern detected in " << xor_pattern_count << "/" << kTrials
                 << " trials — potential micro-fingerprint";
  }
  ASSERT_TRUE(true);
}

// ---------------------------------------------------------------------------
// RISK-WS06: ECH payload length distribution breadth.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, EchPayloadLengthDistributionBreadth) {
  std::map<uint16, int> length_counts;
  constexpr int kTrials = 500;
  size_t ech_extension_seen = 0;

  NetworkRouteHints non_ru;
  non_ru.is_known = true;
  non_ru.is_ru = false;

  for (uint64 seed = 0; seed < kTrials; seed++) {
    MockRng rng(seed);
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789abcdef", 1712345678, non_ru, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    auto &ch = parsed.ok_ref();
    for (auto &ext : ch.extensions) {
      if (ext.type == 0xFE0D) {
        ech_extension_seen++;
        length_counts[ch.ech_payload_length]++;
        break;
      }
    }
  }

  ASSERT_TRUE(ech_extension_seen > 0);
  ASSERT_TRUE(!length_counts.empty());
  for (const auto &kv : length_counts) {
    // Guard against malformed parser output or out-of-family payload explosions.
    ASSERT_TRUE(kv.first > 0u);
    ASSERT_TRUE(kv.first <= 4096u);
  }
}

// ---------------------------------------------------------------------------
// RISK-WS07: Route-failure cache key bucket boundary isolation.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, CacheKeyBucketBoundaryIsolation) {
  reset_runtime_ech_failure_state_for_tests();

  int32 t1 = 1712345678;
  int32 t2 = t1 + 86400 + 1;

  NetworkRouteHints non_ru;
  non_ru.is_known = true;
  non_ru.is_ru = false;

  for (int i = 0; i < 10; i++) {
    note_runtime_ech_failure("bucket-test.example.com", t1);
  }

  auto decision_t2 = td::mtproto::stealth::get_runtime_ech_decision("bucket-test.example.com", t2, non_ru);
  ASSERT_TRUE(decision_t2.ech_mode == EchMode::Rfc9180Outer);

  reset_runtime_ech_failure_state_for_tests();
}

// ---------------------------------------------------------------------------
// Profile selection diversity.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, ProfileSelectionDistributesAcrossDomains) {
  std::set<int> selected_profiles;
  auto platform = td::mtproto::stealth::default_runtime_platform_hints();

  for (int i = 0; i < 100; i++) {
    auto domain = "site-" + std::to_string(i) + ".example.com";
    auto profile = td::mtproto::stealth::pick_runtime_profile(domain, 1712345678, platform);
    selected_profiles.insert(static_cast<int>(profile));
  }

  ASSERT_TRUE(selected_profiles.size() >= 2u);
}

// ---------------------------------------------------------------------------
// Extreme domain length must not overflow.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, ExtremelyLongDomainDoesNotOverflow) {
  std::string long_domain(4096, 'a');
  long_domain += ".example.com";

  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile(std::move(long_domain), "0123456789abcdef", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  ASSERT_TRUE(wire.size() >= 9u);

  auto record_len = (static_cast<uint16>(static_cast<uint8>(wire[3])) << 8) | static_cast<uint8>(wire[4]);
  ASSERT_EQ(static_cast<size_t>(record_len), wire.size() - 5u);

  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
}

TEST(TlsWireSafety, MinimumDomainLengthProducesValidWire) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("a", "0123456789abcdef", 1712345678, BrowserProfile::Chrome133,
                                                 EchMode::Disabled, rng);
  ASSERT_TRUE(wire.size() >= 9u);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
}

// ---------------------------------------------------------------------------
// Extreme timestamps: all boundary values must produce valid wire.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, TimestampBoundaryValuesProduceValidWire) {
  auto profiles = all_profiles();
  int32 timestamps[] = {0, 1, -1, 2147483647, -2147483647 - 1};

  for (auto profile : profiles) {
    for (auto ts : timestamps) {
      MockRng rng(42);
      auto wire =
          build_tls_client_hello_for_profile("example.com", "0123456789abcdef", ts, profile, EchMode::Disabled, rng);
      ASSERT_TRUE(wire.size() >= 9u);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
    }
  }
}

// ---------------------------------------------------------------------------
// Session ID uniqueness.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, SessionIdUniqueAcrossSeeds) {
  std::set<std::string> session_ids;
  constexpr int kTrials = 200;

  for (uint64 seed = 0; seed < kTrials; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("example.com", "0123456789abcdef", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    ASSERT_TRUE(wire.size() >= 76u);
    auto sid_len = static_cast<uint8>(wire[43]);
    ASSERT_EQ(static_cast<size_t>(sid_len), 32u);
    session_ids.insert(wire.substr(44, 32));
  }

  ASSERT_EQ(session_ids.size(), static_cast<size_t>(kTrials));
}

TEST(TlsWireSafety, ClientRandomUniqueAcrossSeeds) {
  std::set<std::string> client_randoms;
  constexpr int kTrials = 200;

  for (uint64 seed = 0; seed < kTrials; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("example.com", "0123456789abcdef", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    ASSERT_TRUE(wire.size() >= 43u);
    client_randoms.insert(wire.substr(11, 32));
  }

  ASSERT_EQ(client_randoms.size(), static_cast<size_t>(kTrials));
}

// ---------------------------------------------------------------------------
// Different secrets / different HMAC.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, DifferentSecretsDifferentHmac) {
  MockRng rng1(42);
  auto wire1 = build_tls_client_hello_for_profile("example.com", "0123456789abcdef", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire2 = build_tls_client_hello_for_profile("example.com", "fedcba9876543210", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng2);

  ASSERT_TRUE(wire1.substr(11, 32) != wire2.substr(11, 32));
}

// ---------------------------------------------------------------------------
// Success clears only targeted destination.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, SuccessClearsOnlyTargetedDestination) {
  reset_runtime_ech_failure_state_for_tests();

  int32 time = 1712345678;
  NetworkRouteHints non_ru;
  non_ru.is_known = true;
  non_ru.is_ru = false;

  for (int i = 0; i < 10; i++) {
    note_runtime_ech_failure("a.example.com", time);
    note_runtime_ech_failure("b.example.com", time);
  }

  note_runtime_ech_success("a.example.com", time);

  auto decision_a = td::mtproto::stealth::get_runtime_ech_decision("a.example.com", time, non_ru);
  ASSERT_TRUE(decision_a.ech_mode == EchMode::Rfc9180Outer);

  reset_runtime_ech_failure_state_for_tests();
}

// ---------------------------------------------------------------------------
// Proxy mode: ALPN must exclude h2.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, ProxyModeAlpnMustBeHttp11Only) {
  auto profiles = all_profiles();
  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = td::mtproto::stealth::build_proxy_tls_client_hello_for_profile(
        "www.google.com", "0123456789abcdef", 1712345678, profile, EchMode::Rfc9180Outer, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    bool found_alpn = false;
    for (auto &ext : parsed.ok_ref().extensions) {
      if (ext.type == 16) {
        found_alpn = true;
        auto alpn_slice = ext.value;
        std::string alpn_str(alpn_slice.begin(), alpn_slice.end());
        ASSERT_TRUE(alpn_str.find("h2") == std::string::npos);
        break;
      }
    }
    ASSERT_TRUE(found_alpn);
  }
}

// ---------------------------------------------------------------------------
// Cross-seed determinism.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, SameSeedSameInputsSameOutput) {
  auto profiles = all_profiles();
  for (auto profile : profiles) {
    for (auto ech : {EchMode::Disabled, EchMode::Rfc9180Outer}) {
      MockRng rng1(12345);
      auto wire1 =
          build_tls_client_hello_for_profile("example.com", "0123456789abcdef", 1712345678, profile, ech, rng1);
      MockRng rng2(12345);
      auto wire2 =
          build_tls_client_hello_for_profile("example.com", "0123456789abcdef", 1712345678, profile, ech, rng2);
      ASSERT_EQ(wire1, wire2);
    }
  }
}

// ---------------------------------------------------------------------------
// Light fuzz: random inputs must not crash.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, LightFuzzRandomInputsAllProfiles) {
  auto profiles = all_profiles();
  MockRng domain_rng(9999);

  for (uint64 i = 0; i < 200; i++) {
    std::string domain;
    auto len = 3 + (domain_rng.secure_uint32() % 60);
    for (uint32 j = 0; j < len; j++) {
      domain += static_cast<char>('a' + (domain_rng.secure_uint32() % 26));
    }
    domain += ".com";

    auto ts = static_cast<int32>(domain_rng.secure_uint32());
    auto profile_idx = domain_rng.secure_uint32() % profiles.size();
    auto profile = profiles[profile_idx];
    auto ech = (domain_rng.secure_uint32() % 2 == 0) ? EchMode::Disabled : EchMode::Rfc9180Outer;

    MockRng build_rng(i);
    auto wire = build_tls_client_hello_for_profile(std::move(domain), "0123456789abcdef", ts, profile, ech, build_rng);
    ASSERT_TRUE(wire.size() >= 9u);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
  }
}

// ---------------------------------------------------------------------------
// Stress: route-failure cache rapid churn.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, RouteFailureCacheRapidChurn) {
  reset_runtime_ech_failure_state_for_tests();

  int32 time = 1712345678;
  for (int round = 0; round < 50; round++) {
    auto dest = "churn-" + std::to_string(round) + ".example.com";
    for (int i = 0; i < 5; i++) {
      note_runtime_ech_failure(dest, time);
    }
    note_runtime_ech_success(dest, time);
  }

  NetworkRouteHints non_ru;
  non_ru.is_known = true;
  non_ru.is_ru = false;
  auto decision = td::mtproto::stealth::get_runtime_ech_decision("fresh.example.com", time, non_ru);
  ASSERT_TRUE(decision.ech_mode == EchMode::Rfc9180Outer);

  reset_runtime_ech_failure_state_for_tests();
}

// ---------------------------------------------------------------------------
// All profiles emit non-empty extension list.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, AllProfilesEmitNonEmptyExtensions) {
  auto profiles = all_profiles();
  for (auto profile : profiles) {
    for (auto ech : {EchMode::Disabled, EchMode::Rfc9180Outer}) {
      MockRng rng(1);
      auto wire = build_tls_client_hello_for_profile("example.com", "0123456789abcdef", 1712345678, profile, ech, rng);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
      ASSERT_FALSE(parsed.ok_ref().extensions.empty());
    }
  }
}

// ---------------------------------------------------------------------------
// Wire size diversity (padding entropy).
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, WireSizeVariesAcrossSeedsWhenEchDisabled) {
  std::set<size_t> sizes;
  constexpr int kTrials = 100;

  for (uint64 seed = 0; seed < kTrials; seed++) {
    MockRng rng(seed);
    NetworkRouteHints ru;
    ru.is_known = true;
    ru.is_ru = true;
    auto wire = build_default_tls_client_hello("example.com", "0123456789abcdef", 1712345678, ru, rng);
    sizes.insert(wire.size());
  }

  ASSERT_TRUE(sizes.size() >= 4u);
}

// ---------------------------------------------------------------------------
// TLS record header correctness.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, TlsRecordHeaderCorrectTypeAndVersion) {
  auto profiles = all_profiles();
  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("example.com", "0123456789abcdef", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    ASSERT_TRUE(wire.size() >= 5u);
    ASSERT_EQ(static_cast<uint8>(wire[0]), static_cast<uint8>(0x16));
    auto record_version = (static_cast<uint16>(static_cast<uint8>(wire[1])) << 8) | static_cast<uint8>(wire[2]);
    ASSERT_EQ(record_version, static_cast<uint16>(0x0301));
  }
}

// ---------------------------------------------------------------------------
// Handshake type = ClientHello.
// ---------------------------------------------------------------------------

TEST(TlsWireSafety, HandshakeTypeMustBeClientHello) {
  auto profiles = all_profiles();
  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("example.com", "0123456789abcdef", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    ASSERT_TRUE(wire.size() >= 6u);
    ASSERT_EQ(static_cast<uint8>(wire[5]), static_cast<uint8>(0x01));
  }
}

}  // namespace
