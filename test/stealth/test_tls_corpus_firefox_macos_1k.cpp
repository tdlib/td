// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

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

string build_firefox_macos_hello(Slice domain, int32 unix_time, EchMode ech_mode, uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  return build_tls_client_hello_for_profile(domain.str(), "0123456789secret", unix_time,
                                            BrowserProfile::Firefox149_MacOS26_3, ech_mode, rng);
}

ParsedClientHello parse_firefox_macos_hello(Slice domain, int32 unix_time, EchMode ech_mode, uint64 seed) {
  auto parsed = parse_tls_client_hello(build_firefox_macos_hello(domain, unix_time, ech_mode, seed));
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

TEST(FirefoxMacosDesktopInvariance1k, ExtensionOrderExactMatch) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_macos_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    ASSERT_EQ(firefox149_macos26_3NonGreaseExtensionsWithoutPadding, non_grease_extension_sequence(hello));
  }
}

TEST(FirefoxMacosDesktopInvariance1k, HasNoSessionTicketExtension) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_macos_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    ASSERT_TRUE(find_extension(hello, 0x0023u) == nullptr);
  }
}

TEST(FirefoxMacosDesktopInvariance1k, EchPayloadLengthAlwaysMatchesFixture) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_macos_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
    ASSERT_EQ(firefox149_macos26_3Ech.payload_length, hello.ech_payload_length);
  }
}

TEST(FirefoxMacosDesktopInvariance1k, ExtensionSequenceHasNoVariation) {
  std::set<string> sequences;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_firefox_macos_hello("www.google.com", kUnixTime, EchMode::Rfc9180Outer, seed);
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

}  // namespace