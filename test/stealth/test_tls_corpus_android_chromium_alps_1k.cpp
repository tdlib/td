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

constexpr uint64 kCorpusIterations = 1024;
constexpr int32 kUnixTime = 1712345678;

ParsedClientHello build_android_alps_proxy(uint64 seed) {
  MockRng rng(seed);
  auto parsed = parse_tls_client_hello(build_tls_client_hello_for_profile(
      "www.google.com", "0123456789secret", kUnixTime, BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng));
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

TEST(AndroidChromiumAlpsCorpus1k, Chrome133ProxyAlwaysAdvertisesAlps44CD) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    ASSERT_TRUE(extension_set_non_grease_no_padding(build_android_alps_proxy(seed)).count(kAlpsChrome133Plus) != 0);
  }
}

TEST(AndroidChromiumAlpsCorpus1k, Chrome133ProxyAlwaysAdvertisesEchAndPq) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_android_alps_proxy(seed);
    auto extensions = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(extensions.count(kEchExtensionType) != 0);
    ASSERT_TRUE(std::find(hello.supported_groups.begin(), hello.supported_groups.end(), kPqHybridGroup) !=
                hello.supported_groups.end());
  }
}

TEST(AndroidChromiumAlpsCorpus1k, Chrome133ProxyKeepsFreshConnectionsPskFree) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    ASSERT_TRUE(extension_set_non_grease_no_padding(build_android_alps_proxy(seed)).count(0x0029u) == 0);
  }
}

TEST(AndroidChromiumAlpsCorpus1k, Chrome133ProxySetMatchesLinuxChromeChromiumFamilySansPsk) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    ASSERT_TRUE(kChrome133EchExtensionSet == extension_set_non_grease_no_padding(build_android_alps_proxy(seed)));
  }
}

TEST(AndroidChromiumAlpsCorpus1k, Chrome133ProxyExtensionOrderingShowsChromeShuffle) {
  std::set<string> orderings;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    string key;
    for (auto ext : non_grease_extension_sequence(build_android_alps_proxy(seed))) {
      if (!key.empty()) {
        key += ",";
      }
      key += hex_u16(ext);
    }
    orderings.insert(key);
  }
  ASSERT_TRUE(orderings.size() >= 100u);
}

TEST(AndroidChromiumAlpsCorpus1k, Chrome133ProxyCipherSuitesMatchAndroidBraveCaptureFamily) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto cipher_suites = parse_cipher_suite_vector(build_android_alps_proxy(seed).cipher_suites).move_as_ok();
    cipher_suites.erase(std::remove_if(cipher_suites.begin(), cipher_suites.end(), is_grease_value),
                        cipher_suites.end());
    ASSERT_EQ(brave188_138_android13NonGreaseCipherSuites, cipher_suites);
  }
}

TEST(AndroidChromiumAlpsCorpus1k, ChromiumAlpsProxyStaysDistinctFromAndroidOkHttpAdvisoryAndNoAlpsFixtures) {
  auto android_no_alps_fixture = make_unordered_set(chrome146_177_android16NonGreaseExtensionsWithoutPadding);
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto proxy_extensions = extension_set_non_grease_no_padding(build_android_alps_proxy(seed));
    ASSERT_TRUE(proxy_extensions.count(kAlpsChrome133Plus) != 0);
    ASSERT_TRUE(proxy_extensions != android_no_alps_fixture);
  }
}

}  // namespace