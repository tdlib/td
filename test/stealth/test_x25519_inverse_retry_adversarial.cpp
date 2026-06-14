// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/ClientHelloExecutor.h"

#include "test/stealth/TlsHelloParsers.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::ClientHelloExecutor;
using td::mtproto::ClientHelloOp;
using td::mtproto::ExecutorConfig;
using td::mtproto::stealth::IRng;
using td::mtproto::test::is_valid_curve25519_public_coordinate;

class DegenerateX25519SeedRng final : public IRng {
 public:
  void fill_secure_bytes(td::MutableSlice dest) override {
    dest.fill('\0');
    if (fill_calls_ == 0) {
      // Candidate x = 1 in the executor's binary interpretation. Under the buggy helper this passes the first
      // quadratic-residue gate, doubles to x = 0, then hits a zero-denominator
      // inversion on the second doubling and emits the all-zero point.
      dest[0] = '\x01';
    } else {
      for (size_t i = 0; i < dest.size(); i++) {
        dest[i] = static_cast<char>(0xA5u ^ static_cast<td::uint8>(i));
      }
    }
    fill_calls_++;
  }

  td::uint32 secure_uint32() override {
    return 0xA5A5A5A5u;
  }

  td::uint32 bounded(td::uint32 n) override {
    return n == 0 ? 0u : 0u;
  }

 private:
  size_t fill_calls_{0};
};

TEST(X25519InverseRetryAdversarial, DegenerateCandidateRetriesInsteadOfEmittingZeroPoint) {
  DegenerateX25519SeedRng rng;
  std::vector<ClientHelloOp> ops;
  ops.push_back(ClientHelloOp::zero_bytes(64));
  ops.push_back(ClientHelloOp::x25519_public_key());

  auto result =
      ClientHelloExecutor::execute(ops, "retry.example.com", "0123456789secret", 1712345678, ExecutorConfig{}, rng);
  ASSERT_TRUE(result.is_ok());
  auto public_key = result.ok().substr(64);
  ASSERT_EQ(32u, public_key.size());
  ASSERT_TRUE(public_key != td::string(32, '\0'));
  ASSERT_TRUE(is_valid_curve25519_public_coordinate(public_key));
}

}  // namespace
