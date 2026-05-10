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

#include <algorithm>
#include <set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;
using namespace td::mtproto::test::fixtures;
using namespace td::mtproto::test::fixtures::reviewed;

constexpr uint64 kCorpusIterations = kQuickIterations;
constexpr int32 kUnixTime = 1712345678;

string build_safari_hello(uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, BrowserProfile::Safari26_3,
                                            EchMode::Disabled, rng);
}

ParsedClientHello parse_safari_hello(uint64 seed) {
  auto parsed = parse_tls_client_hello(build_safari_hello(seed));
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

vector<uint16> non_grease_cipher_suites(const ParsedClientHello &hello) {
  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  cipher_suites.erase(std::remove_if(cipher_suites.begin(), cipher_suites.end(), is_grease_value), cipher_suites.end());
  return cipher_suites;
}

vector<uint16> non_grease_supported_groups(const ParsedClientHello &hello) {
  auto groups = hello.supported_groups;
  groups.erase(std::remove_if(groups.begin(), groups.end(), is_grease_value), groups.end());
  return groups;
}

vector<uint16> non_grease_supported_versions(const ParsedClientHello &hello) {
  auto *supported_versions = find_extension(hello, 0x002Bu);
  CHECK(supported_versions != nullptr);
  CHECK(!supported_versions->value.empty());

  const auto versions_len = static_cast<uint8>(supported_versions->value[0]);
  CHECK((versions_len % 2) == 0);
  CHECK(static_cast<size_t>(versions_len + 1) == supported_versions->value.size());

  vector<uint16> versions;
  versions.reserve(versions_len / 2);
  for (size_t i = 1; i + 1 < supported_versions->value.size(); i += 2) {
    auto version = static_cast<uint16>((static_cast<uint8>(supported_versions->value[i]) << 8) |
                                       static_cast<uint8>(supported_versions->value[i + 1]));
    versions.push_back(version);
  }

  versions.erase(std::remove_if(versions.begin(), versions.end(), is_grease_value), versions.end());
  return versions;
}

TEST(Safari26_3Invariance1k, NonGreaseCipherSuitesExactMatchCaptureFamily) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    ASSERT_EQ(safari26_3_1_ios26_3_1_aNonGreaseCipherSuites, non_grease_cipher_suites(parse_safari_hello(seed)));
  }
}

TEST(Safari26_3Invariance1k, NonGreaseExtensionOrderExactMatchCaptureFamily) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    ASSERT_EQ(safari26_3_1_ios26_3_1_aNonGreaseExtensionsWithoutPadding,
              non_grease_extension_sequence(parse_safari_hello(seed)));
  }
}

TEST(Safari26_3Invariance1k, NonGreaseSupportedGroupsExactMatchCaptureFamily) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    ASSERT_EQ(safari26_3_1_ios26_3_1_aNonGreaseSupportedGroups, non_grease_supported_groups(parse_safari_hello(seed)));
  }
}

TEST(Safari26_3Invariance1k, NonGreaseSupportedVersionsExactMatchCaptureFamily) {
  const vector<uint16> expected_versions = {0x0304u, 0x0303u};

  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    ASSERT_EQ(expected_versions, non_grease_supported_versions(parse_safari_hello(seed)));
  }
}

TEST(Safari26_3Invariance1k, SupportedVersionsNeverAdvertiseTls10OrTls11) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto versions = non_grease_supported_versions(parse_safari_hello(seed));
    ASSERT_TRUE(std::find(versions.begin(), versions.end(), 0x0302u) == versions.end());
    ASSERT_TRUE(std::find(versions.begin(), versions.end(), 0x0301u) == versions.end());
  }
}

TEST(Safari26_3Invariance1k, NonGreaseExtensionOrderHasNoVariationAcrossSeeds) {
  std::set<string> sequences;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_safari_hello(seed);
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

TEST(Safari26_3Invariance1k, ECHNeverPresent) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_safari_hello(seed);
    ASSERT_TRUE(find_extension(hello, fixtures::kEchExtensionType) == nullptr);
  }
}

TEST(Safari26_3Invariance1k, SessionTicketNeverPresent) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_safari_hello(seed);
    ASSERT_TRUE(find_extension(hello, 0x0023u) == nullptr);
  }
}

TEST(Safari26_3Invariance1k, AlpsNeverPresent) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_safari_hello(seed);
    auto extensions = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(extensions.count(kAlpsChrome131) == 0);
    ASSERT_TRUE(extensions.count(kAlpsChrome133Plus) == 0);
  }
}

TEST(Safari26_3Invariance1k, KeyShareHasGreasePlusPqPlusX25519) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_safari_hello(seed);
    ASSERT_EQ(3u, hello.key_share_entries.size());
    ASSERT_TRUE(is_grease_value(hello.key_share_entries[0].group));
    ASSERT_EQ(kPqHybridGroup, hello.key_share_entries[1].group);
    ASSERT_EQ(kPqHybridKeyShareLength, hello.key_share_entries[1].key_length);
    ASSERT_EQ(kX25519Group, hello.key_share_entries[2].group);
    ASSERT_EQ(kX25519KeyShareLength, hello.key_share_entries[2].key_length);
  }
}

TEST(Safari26_3Invariance1k, CompressCertificateBodyMatchesCaptureFamily) {
  static const char kExpectedBody[] = "\x02\x00\x01";
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_safari_hello(seed);
    auto *compress_certificate = find_extension(hello, 0x001Bu);
    ASSERT_TRUE(compress_certificate != nullptr);
    ASSERT_EQ(Slice(kExpectedBody, 3), compress_certificate->value);
  }
}

TEST(Safari26_3Invariance1k, GreaseAnchorsRemainPresent) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_safari_hello(seed);
    ASSERT_FALSE(hello.extensions.empty());
    ASSERT_TRUE(is_grease_value(hello.extensions.front().type));
    ASSERT_TRUE(is_grease_value(hello.extensions.back().type));
  }
}

TEST(Safari26_3Invariance1k, GreaseCipherAndGroupSlotsRemainPresent) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_safari_hello(seed);
    auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
    ASSERT_FALSE(cipher_suites.empty());
    ASSERT_TRUE(is_grease_value(cipher_suites.front()));
    ASSERT_FALSE(hello.supported_groups.empty());
    ASSERT_TRUE(is_grease_value(hello.supported_groups.front()));
  }
}

}  // namespace