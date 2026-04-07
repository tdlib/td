// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedClientHelloFixtures.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsHelloWireMutator.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;
using namespace td::mtproto::test::fixtures::reviewed;

string build_firefox148_ech(uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                            BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
}

TEST(FirefoxBatch1Ech, ReviewedCorpusPinsDistinctAeadFamiliesFor148And149) {
  ASSERT_EQ(0x0003u, kFirefoxBatch1ReferenceEch.aead_id);
  ASSERT_EQ(0x0001u, kFirefox149Batch1ReferenceEch.aead_id);

  ASSERT_EQ(kFirefoxBatch1ReferenceExtensionOrder, kFirefox149Batch1ReferenceExtensionOrder);
  ASSERT_EQ(kFirefoxBatch1ReferenceSupportedGroups, kFirefox149Batch1ReferenceSupportedGroups);
  ASSERT_EQ(kFirefoxBatch1ReferenceKeyShareEntries.size(), kFirefox149Batch1ReferenceKeyShareEntries.size());
}

TEST(FirefoxBatch1Ech, ParserAcceptsReviewedFirefox148AeadVariant) {
  auto wire = build_firefox148_ech(7);
  ASSERT_TRUE(mutate_ech_value_prefix(wire, kFirefoxBatch1ReferenceEch.outer_type, kFirefoxBatch1ReferenceEch.kdf_id,
                                      kFirefoxBatch1ReferenceEch.aead_id));

  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_EQ(kFirefoxBatch1ReferenceEch.outer_type, parsed.ok().ech_outer_type);
  ASSERT_EQ(kFirefoxBatch1ReferenceEch.kdf_id, parsed.ok().ech_kdf_id);
  ASSERT_EQ(kFirefoxBatch1ReferenceEch.aead_id, parsed.ok().ech_aead_id);
}

TEST(FirefoxBatch1Ech, ParserRejectsUnknownFirefoxLikeAeadVariant) {
  auto wire = build_firefox148_ech(8);
  ASSERT_TRUE(
      mutate_ech_value_prefix(wire, kFirefoxBatch1ReferenceEch.outer_type, kFirefoxBatch1ReferenceEch.kdf_id, 0x0002));
  ASSERT_TRUE(parse_tls_client_hello(wire).is_error());
}

TEST(FirefoxBatch1Ech, Firefox148RuntimeMatchesReviewedBatch1EchMetadata) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto parsed = parse_tls_client_hello(build_firefox148_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(kFirefoxBatch1ReferenceEch.outer_type, parsed.ok().ech_outer_type);
    ASSERT_EQ(kFirefoxBatch1ReferenceEch.kdf_id, parsed.ok().ech_kdf_id);
    ASSERT_EQ(kFirefoxBatch1ReferenceEch.aead_id, parsed.ok().ech_aead_id);
    ASSERT_EQ(kFirefoxBatch1ReferenceEch.enc_length, parsed.ok().ech_declared_enc_length);
    ASSERT_EQ(kFirefoxBatch1ReferenceEch.payload_length, parsed.ok().ech_payload_length);
  }
}

}  // namespace