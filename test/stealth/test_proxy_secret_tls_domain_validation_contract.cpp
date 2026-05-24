// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/ProxySecret.h"

#include "td/utils/misc.h"
#include "td/utils/tests.h"

namespace {

td::string make_tls_emulation_secret(td::Slice domain) {
  td::string secret;
  secret.reserve(17 + domain.size());
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789abcdef";
  secret += domain.str();
  return secret;
}

td::string make_max_length_valid_domain() {
  td::string first_label(63, 'a');
  td::string second_label(63, 'b');
  td::string third_label(td::mtproto::ProxySecret::MAX_DOMAIN_LENGTH - first_label.size() - second_label.size() - 2,
                         'c');
  return first_label + "." + second_label + "." + third_label;
}

TEST(ProxySecretTlsDomainValidationContract, AcceptsCanonicalAsciiHostname) {
  auto r_secret = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret("cdn.example.com"));

  ASSERT_TRUE(r_secret.is_ok());
  auto secret = r_secret.move_as_ok();
  ASSERT_TRUE(secret.emulate_tls());
  ASSERT_EQ(td::string("0123456789abcdef"), secret.get_proxy_secret().str());
}

TEST(ProxySecretTlsDomainValidationContract, LegacySecretModesRemainAccepted) {
  auto r_plain = td::mtproto::ProxySecret::from_binary("0123456789abcdef");
  ASSERT_TRUE(r_plain.is_ok());
  ASSERT_FALSE(r_plain.ok().emulate_tls());

  td::string random_padding_secret;
  random_padding_secret.push_back(static_cast<char>(0xdd));
  random_padding_secret += "0123456789abcdef";

  auto r_random_padding = td::mtproto::ProxySecret::from_binary(random_padding_secret);
  ASSERT_TRUE(r_random_padding.is_ok());
  ASSERT_FALSE(r_random_padding.ok().emulate_tls());
}

TEST(ProxySecretTlsDomainValidationContract, RejectsEmbeddedNulInTlsDomain) {
  const td::string domain("good\0bad.example", 16);

  auto r_secret = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret(domain));

  ASSERT_TRUE(r_secret.is_error());
}

TEST(ProxySecretTlsDomainValidationContract, RejectsControlBytesInTlsDomain) {
  const td::string domain("bad\nname.example", 16);

  auto r_secret = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret(domain));

  ASSERT_TRUE(r_secret.is_error());
}

TEST(ProxySecretTlsDomainValidationContract, RejectsNonAsciiBytesInTlsDomain) {
  const td::string domain("bad\xffname.example", 16);

  auto r_secret = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret(domain));

  ASSERT_TRUE(r_secret.is_error());
}

TEST(ProxySecretTlsDomainValidationContract, RejectsLabelLongerThan63Bytes) {
  td::string long_label(64, 'a');
  auto domain = long_label + ".example.com";

  auto r_secret = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret(domain));

  ASSERT_TRUE(r_secret.is_error());
}

TEST(ProxySecretTlsDomainValidationContract, AcceptsLabelExactly63Bytes) {
  td::string max_label(63, 'a');
  auto domain = max_label + ".example.com";

  auto r_secret = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret(domain));

  ASSERT_TRUE(r_secret.is_ok());
}

TEST(ProxySecretTlsDomainValidationContract, RejectsLabelStartingWithHyphen) {
  auto r_leading = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret("-edge.example.com"));
  auto r_mid = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret("good.-edge.example.com"));

  ASSERT_TRUE(r_leading.is_error());
  ASSERT_TRUE(r_mid.is_error());
}

TEST(ProxySecretTlsDomainValidationContract, RejectsLabelEndingWithHyphen) {
  auto r_trailing = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret("edge-.example.com"));
  auto r_mid = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret("good.edge-.example.com"));

  ASSERT_TRUE(r_trailing.is_error());
  ASSERT_TRUE(r_mid.is_error());
}

TEST(ProxySecretTlsDomainValidationContract, AcceptsPunycodeStyleInteriorHyphens) {
  auto r_secret = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret("xn--e1afmkfd.example"));

  ASSERT_TRUE(r_secret.is_ok());
}

TEST(ProxySecretTlsDomainValidationContract, RejectsEmptyLabelInTlsDomain) {
  auto r_secret = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret("good..example.com"));

  ASSERT_TRUE(r_secret.is_error());
}

TEST(ProxySecretTlsDomainValidationContract, RejectsLeadingOrTrailingDotInTlsDomain) {
  auto r_leading_dot = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret(".example.com"));
  auto r_trailing_dot = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret("example.com."));

  ASSERT_TRUE(r_leading_dot.is_error());
  ASSERT_TRUE(r_trailing_dot.is_error());
}

TEST(ProxySecretTlsDomainValidationContract, TruncationFlagDoesNotBypassInvalidTlsDomainChecks) {
  td::string long_label(64, 'b');
  auto domain = long_label + ".example.com";

  auto r_secret = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret(domain), true);

  ASSERT_TRUE(r_secret.is_error());
}

TEST(ProxySecretTlsDomainValidationContract, FromLinkAppliesSameTlsDomainValidation) {
  const td::string domain("good\0bad.example", 16);
  auto encoded = td::hex_encode(make_tls_emulation_secret(domain));

  auto r_secret = td::mtproto::ProxySecret::from_link(encoded);

  ASSERT_TRUE(r_secret.is_error());
}

TEST(ProxySecretTlsDomainValidationContract, TooLongSecretErrorsIncludeLengthContext) {
  td::string overlong_domain(td::mtproto::ProxySecret::MAX_DOMAIN_LENGTH + 1, 'a');
  auto r_secret = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret(overlong_domain), false);

  ASSERT_TRUE(r_secret.is_error());
  auto message = r_secret.error().message().str();
  ASSERT_TRUE(message.find("Too long secret") != td::string::npos);
  ASSERT_TRUE(message.find("raw_length=") != td::string::npos);
  ASSERT_TRUE(message.find("max_length=") != td::string::npos);
}

TEST(ProxySecretTlsDomainValidationContract, UnsupportedMarkerErrorsIncludeMarkerContext) {
  td::string malformed(17, 'x');
  malformed[0] = static_cast<char>(0xab);

  auto r_secret = td::mtproto::ProxySecret::from_binary(malformed);

  ASSERT_TRUE(r_secret.is_error());
  auto message = r_secret.error().message().str();
  ASSERT_TRUE(message.find("Unsupported proxy secret") != td::string::npos);
  ASSERT_TRUE(message.find("raw_length=") != td::string::npos);
  ASSERT_TRUE(message.find("marker=0x") != td::string::npos);
}

TEST(ProxySecretTlsDomainValidationContract, InvalidTlsDomainErrorsIncludeValidationContext) {
  auto r_secret = td::mtproto::ProxySecret::from_binary(make_tls_emulation_secret("bad..example.com"));

  ASSERT_TRUE(r_secret.is_error());
  auto message = r_secret.error().message().str();
  ASSERT_TRUE(message.find("Wrong proxy secret") != td::string::npos);
  ASSERT_TRUE(message.find("domain_length=") != td::string::npos);
  ASSERT_TRUE(message.find("tls_domain_error=") != td::string::npos);
}

TEST(ProxySecretTlsDomainValidationContract, UserSampleHexSecretIsAcceptedAndUsesTlsDomain) {
  const td::string user_sample_secret_hex = "eeffb8ecadb37e893760b73778ed5a4ae97777772e6761727368696e6b612e7275";

  auto r_secret = td::mtproto::ProxySecret::from_link(user_sample_secret_hex);

  ASSERT_TRUE(r_secret.is_ok());
  ASSERT_TRUE(r_secret.ok().emulate_tls());
  ASSERT_EQ(td::string("www.garshinka.ru"), r_secret.ok().get_domain());
}

TEST(ProxySecretTlsDomainValidationContract, CorrectedHexSecretWithAsciiDomainIsAccepted) {
  const td::string corrected_secret_hex = "eeffb8ecadb37e893760b73778ed5a4ae9777777722e6761727368696e6b612e7275";

  auto r_secret = td::mtproto::ProxySecret::from_link(corrected_secret_hex);

  ASSERT_TRUE(r_secret.is_ok());
  ASSERT_TRUE(r_secret.ok().emulate_tls());
  ASSERT_EQ(td::string("wwwr.garshinka.ru"), r_secret.ok().get_domain());
}

TEST(ProxySecretTlsDomainValidationContract, FromLinkRejectsEncodedSecretsLongerThanHexEnvelope) {
  td::string oversized_encoded(2 * (17 + td::mtproto::ProxySecret::MAX_DOMAIN_LENGTH) + 1, 'a');

  auto r_secret = td::mtproto::ProxySecret::from_link(oversized_encoded);

  ASSERT_TRUE(r_secret.is_error());
  auto message = r_secret.error().message().str();
  ASSERT_TRUE(message.find("encoded_length_out_of_bounds") != td::string::npos);
  ASSERT_TRUE(message.find("encoded_length=") != td::string::npos);
  ASSERT_TRUE(message.find("max_encoded_length=") != td::string::npos);
}

TEST(ProxySecretTlsDomainValidationContract, FromLinkAcceptsEncodedSecretAtExactHexEnvelopeBoundary) {
  auto domain = make_max_length_valid_domain();
  auto encoded = td::hex_encode(make_tls_emulation_secret(domain));

  ASSERT_EQ(2 * (17 + td::mtproto::ProxySecret::MAX_DOMAIN_LENGTH), encoded.size());

  auto r_secret = td::mtproto::ProxySecret::from_link(encoded);

  ASSERT_TRUE(r_secret.is_ok());
  ASSERT_TRUE(r_secret.ok().emulate_tls());
  ASSERT_EQ(domain, r_secret.ok().get_domain());
}

TEST(ProxySecretTlsDomainValidationContract, FromLinkNormalModeRejectsSingleByteOverageAtHexBoundary) {
  auto domain = make_max_length_valid_domain();
  auto encoded = td::hex_encode(make_tls_emulation_secret(domain + "x"));

  ASSERT_EQ(2 * (17 + td::mtproto::ProxySecret::MAX_DOMAIN_LENGTH + 1), encoded.size());

  auto r_secret = td::mtproto::ProxySecret::from_link(encoded);

  ASSERT_TRUE(r_secret.is_error());
  auto message = r_secret.error().message().str();
  ASSERT_TRUE(message.find("encoded_length_out_of_bounds") != td::string::npos);
}

TEST(ProxySecretTlsDomainValidationContract, FromLinkTruncationModeAcceptsSingleByteOverageAtHexBoundary) {
  auto domain = make_max_length_valid_domain();
  auto encoded = td::hex_encode(make_tls_emulation_secret(domain + "x"));

  ASSERT_EQ(2 * (17 + td::mtproto::ProxySecret::MAX_DOMAIN_LENGTH + 1), encoded.size());

  auto r_secret = td::mtproto::ProxySecret::from_link(encoded, true);

  ASSERT_TRUE(r_secret.is_ok());
  ASSERT_TRUE(r_secret.ok().emulate_tls());
  ASSERT_EQ(domain, r_secret.ok().get_domain());
}

TEST(ProxySecretTlsDomainValidationContract, FromLinkTruncationModeRejectsEncodedSecretsBeyondOneByteOverage) {
  td::string oversized_encoded(2 * (17 + td::mtproto::ProxySecret::MAX_DOMAIN_LENGTH + 1) + 1, 'a');

  auto r_secret = td::mtproto::ProxySecret::from_link(oversized_encoded, true);

  ASSERT_TRUE(r_secret.is_error());
  auto message = r_secret.error().message().str();
  ASSERT_TRUE(message.find("encoded_length_out_of_bounds") != td::string::npos);
  ASSERT_TRUE(message.find("encoded_length=") != td::string::npos);
  ASSERT_TRUE(message.find("max_encoded_length=") != td::string::npos);
}

}  // namespace
