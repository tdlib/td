//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockRng.h"

#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

#if !TD_DARWIN

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::default_runtime_platform_hints;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::profile_spec;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::test::MockRng;

td::string make_tls_secret(td::Slice domain) {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += domain.str();
  return secret;
}

struct RuntimeFirefoxCandidate final {
  td::string domain;
  td::int32 unix_time{0};
};

RuntimeFirefoxCandidate find_firefox_runtime_candidate() {
  auto platform = default_runtime_platform_hints();
  for (td::uint32 bucket = 20000; bucket < 20384; bucket++) {
    auto unix_time = static_cast<td::int32>(bucket * 86400 + 3600);
    for (td::uint32 i = 0; i < 256; i++) {
      td::string domain = "runtime-firefox-" + td::to_string(i) + ".example.com";
      if (pick_runtime_profile(domain, unix_time, platform) == BrowserProfile::Firefox148) {
        return {std::move(domain), unix_time};
      }
    }
  }
  UNREACHABLE();
  return RuntimeFirefoxCandidate{};
}

TEST(StealthConfigProfileRecordLimit, TlsSecretsUseRuntimeProfileSelectionKey) {
  MockRng rng(1001);
  auto candidate = find_firefox_runtime_candidate();
  auto secret = ProxySecret::from_raw(make_tls_secret(candidate.domain));

  auto config = StealthConfig::from_secret(secret, rng, candidate.unix_time, default_runtime_platform_hints());

  ASSERT_EQ(static_cast<int>(BrowserProfile::Firefox148), static_cast<int>(config.profile));
  ASSERT_EQ(0x4001u, profile_spec(config.profile).record_size_limit);
  ASSERT_EQ(static_cast<td::int32>(profile_spec(config.profile).record_size_limit) - 1,
            config.drs_policy.max_payload_cap);
  ASSERT_TRUE(config.validate().is_ok());
}

TEST(StealthConfigProfileRecordLimit, PlainSecretsDoNotApplyRuntimeTlsProfileSelection) {
  MockRng rng(1002);
  auto candidate = find_firefox_runtime_candidate();
  auto secret = ProxySecret::from_raw("0123456789abcdef");

  auto config = StealthConfig::from_secret(secret, rng, candidate.unix_time, default_runtime_platform_hints());

  ASSERT_EQ(static_cast<int>(BrowserProfile::Chrome133), static_cast<int>(config.profile));
  ASSERT_EQ(0u, profile_spec(config.profile).record_size_limit);
  ASSERT_TRUE(config.validate().is_ok());
}

}  // namespace

#endif