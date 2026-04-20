// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/net/SslCtx.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <array>
#include <random>

#if !TD_EMSCRIPTEN

TEST(SslCtxLightFuzzExtended, MalformedPathsAreRejectedAcrossVerifyModes) {
  const std::array<td::Slice, 8> shards = {
      td::Slice(".."), td::Slice("%2e%2e"), td::Slice("\\\\"), td::Slice("\n"),
      td::Slice("\r"), td::Slice("%00"),    td::Slice("*"),    td::Slice("\t"),
  };

  const std::array<td::uint32, 4> seed_data{0x26042026u, 0xA5A5A5A5u, 0xC0FFEE00u, 0xBADC0DEu};
  std::seed_seq seed(seed_data.begin(), seed_data.end());
  std::mt19937_64 rng(seed);

  for (td::int32 i = 0; i < 10000; ++i) {
    td::string path = "/tmp/";
    path += shards[static_cast<std::size_t>(rng() % shards.size())].str();
    path += "/";
    path += td::to_string(static_cast<td::uint64>(rng()));
    path += ".pem";

    auto on_result = td::SslCtx::create(td::CSlice(path), td::SslCtx::VerifyPeer::On);
    auto off_result = td::SslCtx::create(td::CSlice(path), td::SslCtx::VerifyPeer::Off);

    ASSERT_TRUE(on_result.is_error());
    ASSERT_TRUE(off_result.is_error());
  }
}

TEST(SslCtxLightFuzzExtended, EmptyCertVerifyOnNeverReportsNullContextOnSuccess) {
  constexpr td::int32 kIterations = 4096;

  for (td::int32 i = 0; i < kIterations; ++i) {
    auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On);
    if (result.is_ok()) {
      ASSERT_TRUE(result.ok().get_openssl_ctx() != nullptr);
    }
  }
}

#endif
