// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#if TD_DARWIN && !TD_EMSCRIPTEN

#include "td/net/SslCtx.h"

#include "td/utils/tests.h"

#include <array>
#include <random>

TEST(SslCtxAppleTrustStoreLightFuzz, RandomMalformedExplicitPathsFailForBothModes) {
  const std::array<td::Slice, 5> affixes = {
      td::Slice(".."), td::Slice("%2F"), td::Slice(" "), td::Slice("\\x00"), td::Slice("????"),
  };

  const std::array<td::uint32, 4> seed_data{0x20260420u, 0xA5510u, 0xC0DEC0DEu, 0xBADC0DEu};
  std::seed_seq seed(seed_data.begin(), seed_data.end());
  std::mt19937_64 rng(seed);

  for (td::int32 i = 0; i < 5000; ++i) {
    td::string path = "/tmp/";
    path += affixes[static_cast<std::size_t>(rng() % affixes.size())].str();
    path += "/";
    path += td::to_string(static_cast<td::uint64>(rng()));
    path += ".pem";

    auto on_result = td::SslCtx::create(td::CSlice(path), td::SslCtx::VerifyPeer::On);
    auto off_result = td::SslCtx::create(td::CSlice(path), td::SslCtx::VerifyPeer::Off);
    ASSERT_TRUE(on_result.is_error());
    ASSERT_TRUE(off_result.is_error());
  }
}

TEST(SslCtxAppleTrustStoreLightFuzz, EmptyCertVerifyOnNeverReturnsNullContextOnSuccess) {
  constexpr td::int32 kIterations = 1024;

  for (td::int32 i = 0; i < kIterations; ++i) {
    auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On);
    if (result.is_ok()) {
      ASSERT_TRUE(result.ok().get_openssl_ctx() != nullptr);
    }
  }
}

#endif
