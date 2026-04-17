// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

const uint64 kCorpusIterations = spot_or_full_corpus_iterations();
constexpr int32 kUnixTime = 1712345678;
constexpr size_t kClientRandomOffset = 11;
constexpr size_t kClientRandomLength = 32;

uint64 quick_seed(uint64 iteration_index) {
  return corpus_seed_for_iteration(iteration_index, kQuickIterations);
}

uint64 corpus_seed(uint64 iteration_index) {
  return corpus_seed_for_iteration(iteration_index, kCorpusIterations);
}

struct RuntimeCandidate final {
  string domain;
  int32 unix_time{0};
};

Slice extract_client_random(Slice wire) {
  CHECK(wire.size() >= kClientRandomOffset + kClientRandomLength);
  return wire.substr(kClientRandomOffset, kClientRandomLength);
}

RuntimeCandidate find_runtime_candidate(BrowserProfile target) {
  auto platform = default_runtime_platform_hints();
  for (uint32 bucket = 20000; bucket < 20384; bucket++) {
    auto unix_time = static_cast<int32>(bucket * 86400 + 3600);
    for (uint32 index = 0; index < 256; index++) {
      auto domain = string("dpi-corpus-") + to_string(bucket) + '-' + to_string(index) + ".example.com";
      if (pick_runtime_profile(domain, unix_time, platform) == target) {
        return {domain, unix_time};
      }
    }
  }
  UNREACHABLE();
  return RuntimeCandidate{};
}

ParsedClientHello build_profile_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  auto parsed = parse_tls_client_hello(
      build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng));
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

string build_profile_wire(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
}

TEST(CorpusStatisticalSampling1k, AllProfilesKeepThirtyTwoByteSessionIds) {
  for (auto profile : all_profiles()) {
    for (uint64 seed = 0; seed < kQuickIterations; seed++) {
      ASSERT_EQ(32u, build_profile_hello(profile, EchMode::Disabled, quick_seed(seed)).session_id.size());
    }
  }
}

TEST(CorpusStatisticalSampling1k, Chrome133SessionIdsVaryAcrossConnections) {
  std::unordered_set<string> values;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    values.insert(
        build_profile_hello(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(seed)).session_id.str());
  }
  ASSERT_EQ(kCorpusIterations, values.size());
}

TEST(CorpusStatisticalSampling1k, Chrome133ClientRandomVariesAcrossConnections) {
  std::unordered_set<string> values;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    values.insert(
        extract_client_random(build_profile_wire(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(seed)))
            .str());
  }
  ASSERT_EQ(kCorpusIterations, values.size());
}

TEST(CorpusStatisticalSampling1k, Chrome133X25519KeyShareVariesAcrossConnections) {
  std::unordered_set<string> values;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_profile_hello(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(seed));
    for (const auto &entry : hello.key_share_entries) {
      if (entry.group == fixtures::kX25519Group) {
        values.insert(entry.key_data.str());
      }
    }
  }
  ASSERT_EQ(kCorpusIterations, values.size());
}

TEST(CorpusStatisticalSampling1k, Chrome133PqKeyShareVariesAcrossConnections) {
  std::unordered_set<string> values;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_profile_hello(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(seed));
    for (const auto &entry : hello.key_share_entries) {
      if (entry.group == fixtures::kPqHybridGroup) {
        values.insert(entry.key_data.str());
      }
    }
  }
  ASSERT_EQ(kCorpusIterations, values.size());
}

TEST(CorpusStatisticalSampling1k, Chrome133EchEncKeyVariesAcrossConnections) {
  std::unordered_set<string> values;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_profile_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, corpus_seed(seed));
    values.insert(hello.ech_enc.str());
  }
  ASSERT_EQ(kCorpusIterations, values.size());
}

TEST(CorpusStatisticalSampling1k, NoConsecutiveChrome133ConnectionsReuseFullWireImage) {
  auto previous = build_profile_wire(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(0));
  for (uint64 seed = 1; seed < kCorpusIterations; seed++) {
    auto current = build_profile_wire(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(seed));
    ASSERT_NE(previous, current);
    previous = std::move(current);
  }
}

TEST(CorpusStatisticalSampling1k, RuntimeChrome133RoutePolicyControlsEchAtScale) {
  auto candidate = find_runtime_candidate(BrowserProfile::Chrome133);

  NetworkRouteHints non_ru_route;
  non_ru_route.is_known = true;
  non_ru_route.is_ru = false;

  NetworkRouteHints ru_route;
  ru_route.is_known = true;
  ru_route.is_ru = true;

  NetworkRouteHints unknown_route;
  unknown_route.is_known = false;
  unknown_route.is_ru = false;

  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    MockRng non_ru_rng(quick_seed(seed));
    auto non_ru = parse_tls_client_hello(build_default_tls_client_hello(candidate.domain, "0123456789secret",
                                                                        candidate.unix_time, non_ru_route, non_ru_rng));
    CHECK(non_ru.is_ok());
    ASSERT_TRUE(find_extension(non_ru.ok(), fixtures::kEchExtensionType) != nullptr);

    MockRng ru_rng(quick_seed(seed));
    auto ru = parse_tls_client_hello(
        build_default_tls_client_hello(candidate.domain, "0123456789secret", candidate.unix_time, ru_route, ru_rng));
    CHECK(ru.is_ok());
    ASSERT_TRUE(find_extension(ru.ok(), fixtures::kEchExtensionType) == nullptr);

    MockRng unknown_rng(quick_seed(seed));
    auto unknown = parse_tls_client_hello(build_default_tls_client_hello(
        candidate.domain, "0123456789secret", candidate.unix_time, unknown_route, unknown_rng));
    CHECK(unknown.is_ok());
    ASSERT_TRUE(find_extension(unknown.ok(), fixtures::kEchExtensionType) == nullptr);
  }
}

TEST(CorpusStatisticalSampling1k, ProxyChrome133AdvertisesHttp11OnlyAlpn) {
  static const string kHttp11OnlyAlpn("\x00\x09\x08\x68\x74\x74\x70\x2f\x31\x2e\x31", 11);
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    MockRng rng(quick_seed(seed));
    auto parsed = parse_tls_client_hello(build_proxy_tls_client_hello_for_profile(
        "www.google.com", "0123456789secret", kUnixTime, BrowserProfile::Chrome133, EchMode::Disabled, rng));
    CHECK(parsed.is_ok());
    auto *alpn = find_extension(parsed.ok(), 0x0010u);
    ASSERT_TRUE(alpn != nullptr);
    ASSERT_EQ(kHttp11OnlyAlpn, alpn->value.str());
  }
}

TEST(CorpusStatisticalSampling1k, AllProfilesKeepClientLegacyVersionTls12Marker) {
  for (auto profile : all_profiles()) {
    for (uint64 seed = 0; seed < kQuickIterations; seed++) {
      ASSERT_EQ(0x0303u, build_profile_hello(profile, EchMode::Disabled, quick_seed(seed)).client_legacy_version);
    }
  }
}

}  // namespace