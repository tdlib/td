// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/Handshake.h"
#include "td/mtproto/mtproto_api.h"
#include "td/mtproto/utils.h"

#include "td/telegram/net/NetReliabilityMonitor.h"

#include "td/utils/tests.h"
#include "td/utils/tl_parsers.h"

#include <algorithm>

namespace window_count_duplicate_light_fuzz {

using td::mtproto::AuthKeyHandshake;
using td::mtproto::AuthKeyHandshakeContext;
using td::mtproto::DhCallback;
using td::mtproto::PublicRsaKeyInterface;

class CapturingHandshakeCallback final : public AuthKeyHandshake::Callback {
 public:
  void send_no_crypto(const td::Storer &storer) override {
    td::string message(storer.size(), '\0');
    auto real_size = storer.store(td::MutableSlice(message).ubegin());
    CHECK(real_size == message.size());
    sent_messages.push_back(std::move(message));
  }

  td::vector<td::string> sent_messages;
};

class CountingPublicRsaKey final : public PublicRsaKeyInterface {
 public:
  td::Result<RsaKey> get_rsa_key(const td::vector<td::int64> &fingerprints) override {
    get_rsa_key_calls++;
    last_fingerprints = fingerprints;
    return td::Status::Error("window_count_duplicate_light_fuzz_lookup");
  }

  void drop_keys() override {
    drop_keys_calls++;
  }

  int get_rsa_key_calls{0};
  int drop_keys_calls{0};
  td::vector<td::int64> last_fingerprints;
};

class CountingHandshakeContext final : public AuthKeyHandshakeContext {
 public:
  explicit CountingHandshakeContext(PublicRsaKeyInterface *public_rsa_key) : public_rsa_key_(public_rsa_key) {
  }

  DhCallback *get_dh_callback() override {
    return nullptr;
  }

  PublicRsaKeyInterface *get_public_rsa_key_interface() override {
    return public_rsa_key_;
  }

 private:
  PublicRsaKeyInterface *public_rsa_key_;
};

template <class T>
td::string store_tl_object(const T &object) {
  td::TLObjectStorer<T> storer(object);
  td::string result(storer.size(), '\0');
  auto real_size = storer.store(td::MutableSlice(result).ubegin());
  CHECK(real_size == result.size());
  return result;
}

td::UInt128 extract_req_pq_nonce(td::Slice message) {
  td::TlParser parser(message);
  auto constructor_id = parser.fetch_int();
  CHECK(constructor_id == td::mtproto_api::req_pq_multi::ID);
  td::mtproto_api::req_pq_multi request(parser);
  CHECK(parser.get_error() == nullptr);
  parser.fetch_end();
  CHECK(parser.get_error() == nullptr);
  return request.nonce_;
}

td::UInt128 make_server_nonce() {
  td::UInt128 result;
  for (size_t i = 0; i < sizeof(result.raw); i++) {
    result.raw[i] = static_cast<unsigned char>(0xA0 + i);
  }
  return result;
}

td::string make_res_pq_message(const td::UInt128 &nonce, td::vector<td::int64> fingerprints) {
  td::string pq;
  pq.push_back(static_cast<char>(0x13));
  pq.push_back(static_cast<char>(0x37));
  return store_tl_object(td::mtproto_api::resPQ(
      nonce, make_server_nonce(), pq, td::mtproto_api::array<td::int64>(fingerprints.begin(), fingerprints.end())));
}

td::vector<td::int64> make_seed_matrix(td::uint32 seed) {
  td::vector<td::int64> result;
  const auto base = static_cast<td::int64>(0x1200000000000000LL + static_cast<td::int64>(seed % 7));
  const auto alt = static_cast<td::int64>(0x3400000000000000LL + static_cast<td::int64>((seed * 3) % 11));
  const auto extra = static_cast<td::int64>(0x5600000000000000LL + static_cast<td::int64>((seed * 5) % 13));

  result.push_back(base);
  if (seed % 2 == 0) {
    result.push_back(base);
  }
  if (seed % 3 == 0) {
    result.push_back(alt);
  }
  if (seed % 5 == 0) {
    result.push_back(alt);
  }
  if (seed % 7 == 0) {
    result.push_back(extra);
  }
  if (result.size() == 1) {
    result.push_back(base);
  }
  return result;
}

size_t distinct_count(const td::vector<td::int64> &fingerprints) {
  auto uniq = fingerprints;
  std::sort(uniq.begin(), uniq.end());
  auto unique_end = std::unique(uniq.begin(), uniq.end());
  return static_cast<size_t>(std::distance(uniq.begin(), unique_end));
}

TEST(WindowCountDuplicateLightFuzz, WCDF01) {
  for (td::uint32 seed = 1; seed <= 128; seed++) {
    td::net_health::reset_net_monitor_for_tests();

    AuthKeyHandshake handshake(2, 0);
    CapturingHandshakeCallback callback;
    CountingPublicRsaKey public_rsa_key;
    CountingHandshakeContext context(&public_rsa_key);

    handshake.resume(static_cast<AuthKeyHandshake::Callback *>(&callback));
    ASSERT_EQ(1u, callback.sent_messages.size());

    auto nonce = extract_req_pq_nonce(callback.sent_messages[0]);
    auto fingerprints = make_seed_matrix(seed);
    auto res_pq = make_res_pq_message(nonce, fingerprints);
    auto status = handshake.on_message(res_pq, static_cast<AuthKeyHandshake::Callback *>(&callback), &context);

    auto snapshot = td::net_health::get_net_monitor_snapshot();
    ASSERT_TRUE(status.is_error());
    const bool enough_distinct =
        distinct_count(fingerprints) >= td::mtproto::AuthKeyHandshake::minimum_server_entry_count();
    if (enough_distinct) {
      ASSERT_EQ("window_count_duplicate_light_fuzz_lookup", status.message());
      ASSERT_EQ(1, public_rsa_key.get_rsa_key_calls);
      ASSERT_EQ(1, public_rsa_key.drop_keys_calls);
      ASSERT_EQ(0u, snapshot.counters.low_server_fingerprint_count_total);
    } else {
      ASSERT_EQ(0u, status.message().str().find("Too few server entries"));
      ASSERT_EQ(0, public_rsa_key.get_rsa_key_calls);
      ASSERT_EQ(0, public_rsa_key.drop_keys_calls);
      ASSERT_EQ(1u, snapshot.counters.low_server_fingerprint_count_total);
    }
  }
}

TEST(WindowCountDuplicateLightFuzz, WCDF02) {
  td::net_health::reset_net_monitor_for_tests();

  AuthKeyHandshake handshake(2, 0);
  CapturingHandshakeCallback callback;
  CountingPublicRsaKey public_rsa_key;
  CountingHandshakeContext context(&public_rsa_key);

  handshake.resume(static_cast<AuthKeyHandshake::Callback *>(&callback));
  ASSERT_EQ(1u, callback.sent_messages.size());

  td::vector<td::int64> fingerprints(2048, static_cast<td::int64>(0x1234567890ABCDEFLL));
  auto nonce = extract_req_pq_nonce(callback.sent_messages[0]);
  auto res_pq = make_res_pq_message(nonce, std::move(fingerprints));
  auto status = handshake.on_message(res_pq, static_cast<AuthKeyHandshake::Callback *>(&callback), &context);

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_TRUE(status.is_error());
  ASSERT_EQ(0u, status.message().str().find("Too few server entries"));
  ASSERT_EQ(0, public_rsa_key.get_rsa_key_calls);
  ASSERT_EQ(0, public_rsa_key.drop_keys_calls);
  ASSERT_EQ(1u, snapshot.counters.low_server_fingerprint_count_total);
}

TEST(WindowCountDuplicateLightFuzz, WCDF03) {
  td::net_health::reset_net_monitor_for_tests();

  AuthKeyHandshake handshake(2, 0);
  CapturingHandshakeCallback callback;
  CountingPublicRsaKey public_rsa_key;
  CountingHandshakeContext context(&public_rsa_key);

  handshake.resume(static_cast<AuthKeyHandshake::Callback *>(&callback));
  ASSERT_EQ(1u, callback.sent_messages.size());

  td::vector<td::int64> fingerprints;
  fingerprints.reserve(4096);
  for (size_t i = 0; i < 4096; i++) {
    auto value =
        (i % 5 == 0) ? static_cast<td::int64>(0x0FEDCBA987654321LL) : static_cast<td::int64>(0x0123456789ABCDE0LL);
    fingerprints.push_back(value);
  }

  auto nonce = extract_req_pq_nonce(callback.sent_messages[0]);
  auto res_pq = make_res_pq_message(nonce, std::move(fingerprints));
  auto status = handshake.on_message(res_pq, static_cast<AuthKeyHandshake::Callback *>(&callback), &context);

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_TRUE(status.is_error());
  ASSERT_EQ("window_count_duplicate_light_fuzz_lookup", status.message());
  ASSERT_EQ(1, public_rsa_key.get_rsa_key_calls);
  ASSERT_EQ(1, public_rsa_key.drop_keys_calls);
  ASSERT_EQ(4096u, public_rsa_key.last_fingerprints.size());
  ASSERT_EQ(0u, snapshot.counters.low_server_fingerprint_count_total);
}

}  // namespace window_count_duplicate_light_fuzz