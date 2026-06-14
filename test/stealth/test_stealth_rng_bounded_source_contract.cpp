// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Contract test: every production SecureRng::bounded() implementation used by
// stealth shaping and TLS hello generation must delegate to one shared,
// unbiased rejection-sampling helper. Reintroducing ad hoc modulo reduction in
// one call path silently drifts the distribution contract between stealth
// record shaping and ClientHello generation.

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    auto byte = static_cast<unsigned char>(c);
    if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

TEST(StealthRngBoundedSourceContract, SecureRngBoundedDelegatesToSharedUnbiasedHelper) {
  auto helper = td::mtproto::test::read_repo_text_file("td/mtproto/stealth/SecureRngBounded.h");
  auto stealth_config = td::mtproto::test::read_repo_text_file("td/mtproto/stealth/StealthConfig.cpp");
  auto tls_hello_builder = td::mtproto::test::read_repo_text_file("td/mtproto/stealth/TlsHelloBuilder.cpp");

  auto helper_normalized = normalize_for_contract(helper);
  auto stealth_config_normalized = normalize_for_contract(stealth_config);
  auto tls_hello_builder_normalized = normalize_for_contract(tls_hello_builder);

  ASSERT_TRUE(helper_normalized.find("inlineuint32bounded_secure_uint32(IRng&rng,uint32n){") != td::string::npos);
  ASSERT_TRUE(helper_normalized.find("CHECK(n>0);") != td::string::npos);
  ASSERT_TRUE(helper_normalized.find("autothreshold=static_cast<uint32>(-n)%n;") != td::string::npos);
  ASSERT_TRUE(helper_normalized.find("while(true){autovalue=rng.secure_uint32();if(value>=threshold){returnvalue%n;}}") !=
              td::string::npos);

  ASSERT_TRUE(stealth_config_normalized.find("returnstealth_rng_internal::bounded_secure_uint32(*this,n);") !=
              td::string::npos);
  ASSERT_TRUE(tls_hello_builder_normalized.find("returnstealth_rng_internal::bounded_secure_uint32(*this,n);") !=
              td::string::npos);

  ASSERT_TRUE(stealth_config_normalized.find("Random::secure_uint32()%n") == td::string::npos);
}

}  // namespace
