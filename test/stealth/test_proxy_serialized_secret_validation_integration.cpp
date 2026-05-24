// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/Proxy.h"

#include "td/utils/misc.h"
#include "td/utils/tests.h"
#include "td/utils/tl_helpers.h"

namespace {

struct SerializedProxyRecord {
  td::int32 raw_type{static_cast<td::int32>(td::Proxy::Type::Mtproto)};
  td::string server{"149.154.167.50"};
  td::int32 port{443};
  td::string encoded_secret;

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_int(raw_type);
    storer.store_string(server);
    storer.store_int(port);
    storer.store_string(encoded_secret);
  }
};

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

TEST(ProxySerializedSecretValidationIntegration, PersistedMalformedEncodedSecretFailsClosedDuringParse) {
  SerializedProxyRecord record;
  record.encoded_secret = "@@@not-base64-or-hex@@@";

  td::Proxy proxy;
  auto status = td::unserialize(proxy, td::serialize(record));

  ASSERT_TRUE(status.is_error());
  ASSERT_TRUE(status.message().str().find("Invalid proxy secret") != td::string::npos);
}

TEST(ProxySerializedSecretValidationIntegration, PersistedOverlongDecodedSecretRejectsInvalidTruncationBoundary) {
  SerializedProxyRecord record;
  auto invalid_domain = make_max_length_valid_domain();
  invalid_domain[invalid_domain.size() - 1] = '-';
  record.encoded_secret = td::hex_encode(make_tls_emulation_secret(invalid_domain + "x"));

  td::Proxy proxy;
  auto status = td::unserialize(proxy, td::serialize(record));

  ASSERT_TRUE(status.is_error());
  ASSERT_TRUE(status.message().str().find("Invalid proxy secret") != td::string::npos);
}

TEST(ProxySerializedSecretValidationIntegration, PersistedOverlongDecodedSecretAcceptsValidTruncationBoundary) {
  SerializedProxyRecord record;
  auto valid_domain = make_max_length_valid_domain();
  record.encoded_secret = td::hex_encode(make_tls_emulation_secret(valid_domain + "x"));

  td::Proxy proxy;
  auto status = td::unserialize(proxy, td::serialize(record));

  ASSERT_TRUE(status.is_ok());
  ASSERT_TRUE(proxy.use_mtproto_proxy());
  ASSERT_TRUE(proxy.secret().emulate_tls());
  ASSERT_EQ(valid_domain, proxy.secret().get_domain());
}

TEST(ProxySerializedSecretValidationIntegration, PersistedOversizedEncodedSecretFailsClosedBeforeDecode) {
  SerializedProxyRecord record;
  record.encoded_secret = td::string(2 * (17 + td::mtproto::ProxySecret::MAX_DOMAIN_LENGTH + 1) + 1, 'a');

  td::Proxy proxy;
  auto status = td::unserialize(proxy, td::serialize(record));

  ASSERT_TRUE(status.is_error());
  ASSERT_TRUE(status.message().str().find("Invalid proxy secret") != td::string::npos);
}

}  // namespace
