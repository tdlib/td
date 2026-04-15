// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;
using namespace td::mtproto::test::fixtures;
using namespace td::mtproto::test::fixtures::reviewed;

constexpr uint64 kCorpusIterations = kQuickIterations;
constexpr int32 kUnixTime = 1712345678;

string build_firefox_hello(Slice domain, int32 unix_time, EchMode ech_mode, uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  return build_tls_client_hello_for_profile(domain.str(), "0123456789secret", unix_time, BrowserProfile::Firefox148,
                                            ech_mode, rng);
}

ParsedClientHello parse_firefox_hello(Slice domain, int32 unix_time, EchMode ech_mode, uint64 seed) {
  auto parsed = parse_tls_client_hello(build_firefox_hello(domain, unix_time, ech_mode, seed));
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

TEST(FirefoxLinuxDesktopInvariance1k, AllRunsHaveIdenticalExtensionSequence) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    ASSERT_EQ(kFirefox148ExtensionOrder, non_grease_extension_sequence(hello));
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, NoExtensionOrderVariationAcrossSeeds) {
  std::set<string> sequences;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    string key;
    for (auto ext : non_grease_extension_sequence(hello)) {
      if (!key.empty()) {
        key += ",";
      }
      key += hex_u16(ext);
    }
    sequences.insert(key);
  }
  ASSERT_EQ(1u, sequences.size());
}

TEST(FirefoxLinuxDesktopInvariance1k, NoGreaseInCipherSuites) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
    for (auto cipher_suite : cipher_suites) {
      ASSERT_FALSE(is_grease_value(cipher_suite));
    }
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, NoGreaseInExtensions) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    for (const auto &ext : hello.extensions) {
      ASSERT_FALSE(is_grease_value(ext.type));
    }
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, NoGreaseInSupportedGroups) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    for (auto group : hello.supported_groups) {
      ASSERT_FALSE(is_grease_value(group));
    }
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, CipherSuiteExactOrderAcross1024Runs) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    ASSERT_EQ(firefox148_linux_desktopCipherSuites, parse_cipher_suite_vector(hello.cipher_suites).move_as_ok());
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, EchIsAlwaysLastNonPaddingExtension) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    auto sequence = non_grease_extension_sequence(hello);
    ASSERT_FALSE(sequence.empty());
    ASSERT_EQ(fixtures::kEchExtensionType, sequence.back());
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, EchIsAlwaysLastWithDifferentDomains) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto domain = PSTRING() << "firefox-" << seed << ".example.org";
    auto hello = parse_firefox_hello(domain, kUnixTime, EchMode::Rfc9180Outer, seed);
    auto sequence = non_grease_extension_sequence(hello);
    ASSERT_FALSE(sequence.empty());
    ASSERT_EQ(fixtures::kEchExtensionType, sequence.back());
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, DelegatedCredentialsPresentInAllRuns) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    ASSERT_TRUE(find_extension(hello, 0x0022u) != nullptr);
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, RecordSizeLimitPresentInAllRuns) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    ASSERT_TRUE(find_extension(hello, 0x001Cu) != nullptr);
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, FfdheGroupsPresentInAllRuns) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    auto groups = make_unordered_set(hello.supported_groups);
    ASSERT_TRUE(groups.count(0x0100u) != 0);
    ASSERT_TRUE(groups.count(0x0101u) != 0);
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, KeyShareHasExactlyThreeEntries) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    ASSERT_EQ(3u, hello.key_share_entries.size());
    ASSERT_EQ(0x11ECu, hello.key_share_entries[0].group);
    ASSERT_EQ(0x001Du, hello.key_share_entries[1].group);
    ASSERT_EQ(0x0017u, hello.key_share_entries[2].group);
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, EchPayloadIs239Fixed) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    ASSERT_EQ(239u, hello.ech_payload_length);
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, AlpsExtensionNeverPresent) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    auto extensions = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(extensions.count(kAlpsChrome131) == 0);
    ASSERT_TRUE(extensions.count(kAlpsChrome133Plus) == 0);
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, FuzzedDomainStillProducesFixedOrder) {
  for (uint64 domain_index = 0; domain_index < kCorpusIterations; domain_index++) {
    auto domain = PSTRING() << "fx-" << domain_index << ".corp.invalid";
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_firefox_hello(domain, kUnixTime, EchMode::Rfc9180Outer, seed);
      ASSERT_EQ(kFirefox148ExtensionOrder, non_grease_extension_sequence(hello));
    }
  }
}

TEST(FirefoxLinuxDesktopInvariance1k, FuzzedTimestampStillProducesFixedOrder) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello =
        parse_firefox_hello("www.google.com", static_cast<int32>(kUnixTime + seed), EchMode::Rfc9180Outer, seed);
    ASSERT_EQ(kFirefox148ExtensionOrder, non_grease_extension_sequence(hello));
  }
}

}  // namespace