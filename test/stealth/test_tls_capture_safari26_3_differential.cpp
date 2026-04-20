// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Differential tests against the reviewed Safari 26.3.1 iOS 26.3.1
// capture family from ReviewedClientHelloFixtures.h.

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedClientHelloReferences.h"
#include "test/stealth/TestHelpers.h"

#include <algorithm>
#include <unordered_map>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;
using namespace td::mtproto::test::fixtures;
using namespace td::mtproto::test::fixtures::reviewed_refs;

string build_safari26_3(uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                            BrowserProfile::Safari26_3, EchMode::Disabled, rng);
}

ParsedClientHello parse_safari26_3(Slice client_hello) {
  auto parsed = parse_tls_client_hello(client_hello);
  if (parsed.is_error()) {
    LOG(ERROR) << parsed.error();
  }
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

vector<uint16> non_grease_cipher_suites(Slice client_hello) {
  auto parsed = parse_safari26_3(client_hello);
  auto cipher_suites = parse_cipher_suite_vector(parsed.cipher_suites).move_as_ok();
  cipher_suites.erase(std::remove_if(cipher_suites.begin(), cipher_suites.end(), is_grease_value), cipher_suites.end());
  return cipher_suites;
}

vector<uint16> non_grease_extension_order(Slice client_hello) {
  auto parsed = parse_safari26_3(client_hello);

  vector<uint16> extensions;
  extensions.reserve(parsed.extensions.size());
  for (const auto &extension : parsed.extensions) {
    if (!is_grease_value(extension.type) && extension.type != 0x0015u) {
      extensions.push_back(extension.type);
    }
  }
  return extensions;
}

vector<uint16> non_grease_supported_groups(Slice client_hello) {
  auto groups = parse_safari26_3(client_hello).supported_groups;
  groups.erase(std::remove_if(groups.begin(), groups.end(), is_grease_value), groups.end());
  return groups;
}

std::unordered_map<uint16, uint16> non_grease_key_share_lengths(Slice client_hello) {
  auto parsed = parse_safari26_3(client_hello);

  std::unordered_map<uint16, uint16> result;
  for (const auto &entry : parsed.key_share_entries) {
    if (!is_grease_value(entry.group)) {
      result[entry.group] = entry.key_length;
    }
  }
  return result;
}

TEST(SafariCaptureDifferential, CipherSuitesExactMatchReviewedCaptureFamily) {
  for (uint64 seed = 0; seed < 10; seed++) {
    ASSERT_EQ(safari26_3_1_ios26_3_1_a_non_grease_cipher_suites, non_grease_cipher_suites(build_safari26_3(seed)));
  }
}

TEST(SafariCaptureDifferential, ExtensionOrderExactMatchReviewedCaptureFamily) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto wire = build_safari26_3(seed);
    ASSERT_EQ(safari26_3_1_ios26_3_1_a_non_grease_extensions_without_padding, non_grease_extension_order(wire));
    ASSERT_FALSE(has_extension(wire, 0x0023u));
  }
}

TEST(SafariCaptureDifferential, SupportedGroupsAndKeySharesMatchReviewedCaptureFamily) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto wire = build_safari26_3(seed);
    ASSERT_EQ(safari26_3_1_ios26_3_1_a_non_grease_supported_groups, non_grease_supported_groups(wire));

    auto key_share_lengths = non_grease_key_share_lengths(wire);
    ASSERT_EQ(2u, key_share_lengths.size());
    ASSERT_EQ(kPqHybridKeyShareLength, key_share_lengths[kPqHybridGroup]);
    ASSERT_EQ(kX25519KeyShareLength, key_share_lengths[kX25519Group]);
  }
}

TEST(SafariCaptureDifferential, CompressCertificateBodyMatchesReviewedCaptureFamily) {
  static const char kExpectedBody[] = "\x02\x00\x01";

  for (uint64 seed = 0; seed < 10; seed++) {
    auto body = extract_extension_body(build_safari26_3(seed), 0x001Bu);
    ASSERT_EQ(Slice(kExpectedBody, 3), body);
  }
}

}  // namespace