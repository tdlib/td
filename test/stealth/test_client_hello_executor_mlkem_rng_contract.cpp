// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Contract tests: the ML-KEM coefficient generator must consume the injected
// IRng bounded-sampling seam instead of ad hoc modulo reduction on
// secure_uint32(). The bounded seam is the executor's source of truth for
// unbiased domain-limited draws.

#include "td/mtproto/ClientHelloExecutor.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::ClientHelloExecutor;
using td::mtproto::ClientHelloOp;
using td::mtproto::ExecutorConfig;
using td::mtproto::stealth::IRng;

class MlKemBoundedSamplingRng final : public IRng {
 public:
  void fill_secure_bytes(td::MutableSlice dest) override {
    for (size_t i = 0; i < dest.size(); i++) {
      dest[i] = static_cast<char>(0x5Au ^ static_cast<td::uint8>(i));
    }
  }

  td::uint32 secure_uint32() override {
    secure_uint32_calls_++;
    return 0xFFFFFFFFu;
  }

  td::uint32 bounded(td::uint32 n) override {
    CHECK(n != 0);
    bounded_calls_++;
    auto value = next_bounded_value_ % n;
    next_bounded_value_++;
    return value;
  }

  td::uint32 bounded_calls() const {
    return bounded_calls_;
  }

  td::uint32 secure_uint32_calls() const {
    return secure_uint32_calls_;
  }

 private:
  td::uint32 bounded_calls_{0};
  td::uint32 secure_uint32_calls_{0};
  td::uint32 next_bounded_value_{0};
};

TEST(ClientHelloExecutorMlKemRngContract, HybridMlKemEntryUsesBoundedSamplingSeam) {
  MlKemBoundedSamplingRng rng;
  std::vector<ClientHelloOp> ops;
  ops.push_back(ClientHelloOp::x25519_ml_kem_768_key_share_entry());

  auto result =
      ClientHelloExecutor::execute(ops, "mlkem-contract.example.com", "0123456789secret", 1712345678, ExecutorConfig{},
                                   rng);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(1220u, result.ok().size());
  ASSERT_EQ(768u, rng.bounded_calls());
  ASSERT_EQ(0u, rng.secure_uint32_calls());
}

}  // namespace
