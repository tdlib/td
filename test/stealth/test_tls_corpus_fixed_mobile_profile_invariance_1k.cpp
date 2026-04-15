// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

constexpr uint64 kCorpusIterations = kQuickIterations;
constexpr int32 kUnixTime = 1712345678;

string build_mobile_hello(BrowserProfile profile, uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, EchMode::Disabled,
                                            rng);
}

ParsedClientHello parse_mobile_hello(BrowserProfile profile, uint64 seed) {
  auto parsed = parse_tls_client_hello(build_mobile_hello(profile, seed));
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

uint16 extension_code(const td::mtproto::BrowserExtension &extension) {
  return extension.type == td::mtproto::TlsExtensionType::Custom ? extension.custom_type
                                                                  : static_cast<uint16>(extension.type);
}

vector<uint16> expected_non_grease_extension_order(BrowserProfile profile) {
  vector<uint16> result;
  const auto &spec = td::mtproto::get_profile_spec(profile);
  result.reserve(spec.extensions.size());
  for (const auto &extension : spec.extensions) {
    auto type = extension_code(extension);
    if (!is_grease_value(type) && type != 0x0015) {
      result.push_back(type);
    }
  }
  return result;
}

vector<uint16> expected_non_grease_supported_groups(BrowserProfile profile) {
  return td::mtproto::get_profile_spec(profile).supported_groups;
}

template <class Predicate>
void for_each_fixed_mobile_profile(Predicate &&predicate) {
  for (auto profile : {BrowserProfile::Android11_OkHttp_Advisory}) {
    predicate(profile);
  }
}

TEST(FixedMobileProfileInvariance1k, NonGreaseExtensionOrderFixedAcrossProfilesAndSeeds) {
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    auto expected = expected_non_grease_extension_order(profile);
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      ASSERT_EQ(expected, non_grease_extension_sequence(parse_mobile_hello(profile, seed)));
    }
  });
}

TEST(FixedMobileProfileInvariance1k, NonGreaseExtensionOrderHasNoVariation) {
  std::set<string> orders;
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    auto expected = expected_non_grease_extension_order(profile);
    string expected_key;
    for (auto ext : expected) {
      if (!expected_key.empty()) {
        expected_key += ",";
      }
      expected_key += hex_u16(ext);
    }
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      string key;
      for (auto ext : non_grease_extension_sequence(parse_mobile_hello(profile, seed))) {
        if (!key.empty()) {
          key += ",";
        }
        key += hex_u16(ext);
      }
      orders.insert(key);
    }
    ASSERT_EQ(expected_key, *orders.begin());
  });
  ASSERT_EQ(1u, orders.size());
}

TEST(FixedMobileProfileInvariance1k, SessionTicketAndECHRemainAbsent) {
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_mobile_hello(profile, seed);
      ASSERT_TRUE(find_extension(hello, 0x0023u) == nullptr);
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
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    auto expected = expected_non_grease_supported_groups(profile);
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

TEST(FixedMobileProfileInvariance1k, KeyShareContainsOnlyX25519) {
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_mobile_hello(profile, seed);
      ASSERT_EQ(1u, hello.key_share_entries.size());
      ASSERT_EQ(fixtures::kX25519Group, hello.key_share_entries[0].group);
      ASSERT_EQ(fixtures::kX25519KeyShareLength, hello.key_share_entries[0].key_length);
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

TEST(FixedMobileProfileInvariance1k, CompressCertificateRemainsAbsent) {
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_mobile_hello(profile, seed);
      ASSERT_TRUE(find_extension(hello, 0x001Bu) == nullptr);
    }
  });
}

TEST(FixedMobileProfileInvariance1k, AlpnRemainsH2ThenHttp11) {
  static const char kExpectedBody[] = "\x00\x0c\x02\x68\x32\x08\x68\x74\x74\x70\x2f\x31\x2e\x31";
  for_each_fixed_mobile_profile([&](BrowserProfile profile) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_mobile_hello(profile, seed);
      auto *alpn = find_extension(hello, 0x0010u);
      ASSERT_TRUE(alpn != nullptr);
      ASSERT_EQ(Slice(kExpectedBody, sizeof(kExpectedBody) - 1), alpn->value);
    }
  });
}

}  // namespace