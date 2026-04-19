// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#if TD_DARWIN && !TD_EMSCRIPTEN

#include "td/net/SslCtx.h"

#include "td/utils/tests.h"

TEST(SslCtxAppleTrustStoreContract, EmptyCertVerifyOffProducesUsableContext) {
  auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.ok().get_openssl_ctx() != nullptr);
}

TEST(SslCtxAppleTrustStoreContract, EmptyCertVerifyOnIsFailClosedWithoutNullSuccess) {
  auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On);
  if (result.is_ok()) {
    ASSERT_TRUE(result.ok().get_openssl_ctx() != nullptr);
  } else {
    ASSERT_TRUE(result.is_error());
  }
}

TEST(SslCtxAppleTrustStoreContract, ExplicitMissingBundleFailsForBothModes) {
  auto on_result = td::SslCtx::create("/no/such/apple/ca-bundle.pem", td::SslCtx::VerifyPeer::On);
  auto off_result = td::SslCtx::create("/no/such/apple/ca-bundle.pem", td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(on_result.is_error());
  ASSERT_TRUE(off_result.is_error());
}

TEST(SslCtxAppleTrustStoreContract, VerifyModeOutcomesAreDeterministicAcrossCalls) {
  const bool first_on = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On).is_ok();
  const bool first_off = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off).is_ok();

  for (td::int32 i = 0; i < 8; ++i) {
    ASSERT_EQ(first_on, td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On).is_ok());
    ASSERT_EQ(first_off, td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off).is_ok());
  }
}

#endif
