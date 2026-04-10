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

constexpr uint64 kCorpusIterations = 1024;
constexpr int32 kUnixTime = 1712345678;

string build_mobile_hello(BrowserProfile profile, uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, EchMode::Disabled,
                                            rng);
}

ParsedClientHello parse_mobile_hello(BrowserProfile profile, uint64 seed) {
  auto parsed = parse_tls_client_hello(build_mobile_hello(profile, seed));
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

vector<uint16> expected_non_grease_extension_order() {
  return {0x0000, 0x0005, 0x000A, 0x000B, 0x000D, 0x0010, 0x0012,
          0x0017, 0x001B, 0x0023, 0x002B, 0x002D, 0x0033, 0xFF01};
}

vector<uint16> expected_non_grease_supported_groups() {
  return {0x001D, 0x0017, 0x0018};
}

template <class Predicate>
void for_each_fixed_mobile_profile(Predicate &&predicate) {
  for (auto profile : {BrowserProfile::Android11_OkHttp_Advisory}) {
    predicate(profile);
  }
}

TEST(FixedMobileProfileInvariance1k, NonGreaseExtensionOrderFixedAcrossProfilesAndSeeds) {
  auto expected = expected_non_grease_extension_order();
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      ASSERT_EQ(expected, non_grease_extension_sequence(parse_mobile_hello(profile, seed)));
    }
  });
}

TEST(FixedMobileProfileInvariance1k, NonGreaseExtensionOrderHasNoVariation) {
  std::set<string> orders;
  auto expected = expected_non_grease_extension_order();
  string expected_key;
  for (auto ext : expected) {
    if (!expected_key.empty()) {
      expected_key += ",";
    }
    expected_key += hex_u16(ext);
  }
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < 128; seed++) {
      string key;
      for (auto ext : non_grease_extension_sequence(parse_mobile_hello(profile, seed))) {
        if (!key.empty()) {
          key += ",";
        }
        key += hex_u16(ext);
      }
      orders.insert(key);
    }
  });
  ASSERT_EQ(1u, orders.size());
  ASSERT_EQ(expected_key, *orders.begin());
}

TEST(FixedMobileProfileInvariance1k, SessionTicketAlwaysPresentAndECHAlwaysAbsent) {
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_mobile_hello(profile, seed);
      ASSERT_TRUE(find_extension(hello, 0x0023u) != nullptr);
      ASSERT_TRUE(find_extension(hello, fixtures::kEchExtensionType) == nullptr);
    }
  });
}

TEST(FixedMobileProfileInvariance1k, AlpsAndDelegatedCredentialsNeverPresent) {
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_mobile_hello(profile, seed);
      auto extensions = extension_set_non_grease_no_padding(hello);
      ASSERT_TRUE(extensions.count(fixtures::kAlpsChrome131) == 0);
      ASSERT_TRUE(extensions.count(fixtures::kAlpsChrome133Plus) == 0);
      ASSERT_TRUE(extensions.count(0x0022u) == 0);
      ASSERT_TRUE(extensions.count(0x001Cu) == 0);
    }
  });
}

TEST(FixedMobileProfileInvariance1k, SupportedGroupsStayLegacyThreeGroupSet) {
  auto expected = expected_non_grease_supported_groups();
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_mobile_hello(profile, seed);
      ASSERT_EQ(expected, [&] {
        auto groups = hello.supported_groups;
        groups.erase(std::remove_if(groups.begin(), groups.end(), is_grease_value), groups.end());
        return groups;
      }());
    }
  });
}

TEST(FixedMobileProfileInvariance1k, KeyShareContainsOnlyGreaseAndX25519) {
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_mobile_hello(profile, seed);
      ASSERT_EQ(2u, hello.key_share_entries.size());
      ASSERT_TRUE(is_grease_value(hello.key_share_entries[0].group));
      ASSERT_EQ(fixtures::kX25519Group, hello.key_share_entries[1].group);
      ASSERT_EQ(fixtures::kX25519KeyShareLength, hello.key_share_entries[1].key_length);
    }
  });
}

TEST(FixedMobileProfileInvariance1k, GreaseAnchorsRemainPresent) {
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_mobile_hello(profile, seed);
      ASSERT_FALSE(hello.extensions.empty());
      ASSERT_TRUE(is_grease_value(hello.extensions.front().type));
      ASSERT_TRUE(is_grease_value(hello.extensions.back().type));
    }
  });
}

TEST(FixedMobileProfileInvariance1k, GreaseCipherAndGroupSlotsRemainPresent) {
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_mobile_hello(profile, seed);
      auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
      ASSERT_FALSE(cipher_suites.empty());
      ASSERT_TRUE(is_grease_value(cipher_suites.front()));
      ASSERT_FALSE(hello.supported_groups.empty());
      ASSERT_TRUE(is_grease_value(hello.supported_groups.front()));
    }
  });
}

TEST(FixedMobileProfileInvariance1k, CompressCertificateBodyIsBrotliOnly) {
  static const char kExpectedBody[] = "\x02\x00\x02";
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_mobile_hello(profile, seed);
      auto *compress_certificate = find_extension(hello, 0x001Bu);
      ASSERT_TRUE(compress_certificate != nullptr);
      ASSERT_EQ(Slice(kExpectedBody, 3), compress_certificate->value);
    }
  });
}

TEST(FixedMobileProfileInvariance1k, AlpnRemainsH2ThenHttp11) {
  static const char kExpectedBody[] = "\x00\x0c\x02\x68\x32\x08\x68\x74\x74\x70\x2f\x31\x2e\x31";
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < 128; seed++) {
      auto hello = parse_mobile_hello(profile, seed);
      auto *alpn = find_extension(hello, 0x0010u);
      ASSERT_TRUE(alpn != nullptr);
      ASSERT_EQ(Slice(kExpectedBody, sizeof(kExpectedBody) - 1), alpn->value);
    }
  });
}

}  // namespace