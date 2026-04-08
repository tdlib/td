// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
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

TEST(FirefoxLinuxDesktopEch, ReviewedCorpusPinsDistinctAeadFamiliesFor148And149) {
  ASSERT_EQ(0x0003u, kFirefoxLinuxDesktopReferenceEch.aead_id);
  ASSERT_EQ(0x0001u, kFirefox149LinuxDesktopReferenceEch.aead_id);

  ASSERT_EQ(kFirefoxLinuxDesktopReferenceExtensionOrder, kFirefox149LinuxDesktopReferenceExtensionOrder);
  ASSERT_EQ(kFirefoxLinuxDesktopReferenceSupportedGroups, kFirefox149LinuxDesktopReferenceSupportedGroups);
  ASSERT_EQ(kFirefoxLinuxDesktopReferenceKeyShareEntries.size(),
            kFirefox149LinuxDesktopReferenceKeyShareEntries.size());
}

TEST(FirefoxLinuxDesktopEch, ParserAcceptsReviewedFirefox148AeadVariant) {
  auto wire = build_firefox148_ech(7);
  ASSERT_TRUE(mutate_ech_value_prefix(wire, kFirefoxLinuxDesktopReferenceEch.outer_type,
                                      kFirefoxLinuxDesktopReferenceEch.kdf_id,
                                      kFirefoxLinuxDesktopReferenceEch.aead_id));

  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_EQ(kFirefoxLinuxDesktopReferenceEch.outer_type, parsed.ok().ech_outer_type);
  ASSERT_EQ(kFirefoxLinuxDesktopReferenceEch.kdf_id, parsed.ok().ech_kdf_id);
  ASSERT_EQ(kFirefoxLinuxDesktopReferenceEch.aead_id, parsed.ok().ech_aead_id);
}

TEST(FirefoxLinuxDesktopEch, ParserRejectsUnknownFirefoxLikeAeadVariant) {
  auto wire = build_firefox148_ech(8);
  ASSERT_TRUE(mutate_ech_value_prefix(wire, kFirefoxLinuxDesktopReferenceEch.outer_type,
                                      kFirefoxLinuxDesktopReferenceEch.kdf_id, 0x0002));
  ASSERT_TRUE(parse_tls_client_hello(wire).is_error());
}

TEST(FirefoxLinuxDesktopEch, Firefox148RuntimeMatchesReviewedLinuxDesktopEchMetadata) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto parsed = parse_tls_client_hello(build_firefox148_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(kFirefoxLinuxDesktopReferenceEch.outer_type, parsed.ok().ech_outer_type);
    ASSERT_EQ(kFirefoxLinuxDesktopReferenceEch.kdf_id, parsed.ok().ech_kdf_id);
    ASSERT_EQ(kFirefoxLinuxDesktopReferenceEch.aead_id, parsed.ok().ech_aead_id);
    ASSERT_EQ(kFirefoxLinuxDesktopReferenceEch.enc_length, parsed.ok().ech_declared_enc_length);
    ASSERT_EQ(kFirefoxLinuxDesktopReferenceEch.payload_length, parsed.ok().ech_payload_length);
  }
}

}  // namespace