//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::parse_tls_client_hello;

td::string build_ech_enabled_client_hello(td::int32 unix_time) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;
  return build_default_tls_client_hello("www.google.com", "0123456789secret", unix_time, hints);
}

class XorShift64 final {
 public:
  explicit XorShift64(td::uint64 seed) : state_(seed == 0 ? 0x9e3779b97f4a7c15ULL : seed) {
  }

  td::uint32 bounded(td::uint32 n) {
    CHECK(n > 0);
    return static_cast<td::uint32>(next_u64() % n);
  }

 private:
  td::uint64 state_;

  td::uint64 next_u64() {
    auto x = state_;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    state_ = x;
    return x;
  }
};

TEST(TlsHelloParserFuzz, TruncatedInputsAreRejected) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  for (size_t cut = 0; cut < wire.size(); cut++) {
    auto parsed = parse_tls_client_hello(td::Slice(wire).substr(0, cut));
    ASSERT_TRUE(parsed.is_error());
  }
}

TEST(TlsHelloParserFuzz, MutationsMustNotBypassCoreInvariants) {
  auto wire = build_ech_enabled_client_hello(1712345678);

  XorShift64 rng(0x1234abcdULL);
  size_t rejected = 0;

  for (int i = 0; i < 256; i++) {
    td::string mutated = wire;
    auto flips = 1u + rng.bounded(4);
    for (td::uint32 j = 0; j < flips; j++) {
      auto pos = rng.bounded(static_cast<td::uint32>(mutated.size()));
      auto bit = static_cast<unsigned char>(1u << rng.bounded(8));
      mutated[pos] = static_cast<char>(static_cast<unsigned char>(mutated[pos]) ^ bit);
    }

    auto parsed = parse_tls_client_hello(mutated);
    if (parsed.is_error()) {
      rejected++;
      continue;
    }

    const auto &hello = parsed.ok();
    ASSERT_EQ(0x16, hello.record_type);
    ASSERT_EQ(0x01, hello.handshake_type);

    std::unordered_set<td::uint16> supported_groups(hello.supported_groups.begin(), hello.supported_groups.end());
    for (auto group : hello.key_share_groups) {
      ASSERT_TRUE(supported_groups.count(group) != 0);
    }

    ASSERT_EQ(hello.ech_declared_enc_length, hello.ech_actual_enc_length);
  }

  ASSERT_TRUE(rejected > 0u);
}

}  // namespace
