// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/net/SslCtx.h"

#include "td/utils/port/FileFd.h"
#include "td/utils/tests.h"

#if !TD_EMSCRIPTEN

namespace {

td::CSlice find_system_bundle() {
  static constexpr td::CSlice kCandidates[] = {
      "/etc/ssl/certs/ca-certificates.crt",
      "/etc/pki/tls/certs/ca-bundle.crt",
      "/etc/ssl/ca-bundle.pem",
  };

  for (auto candidate : kCandidates) {
    auto r_file = td::FileFd::open(candidate.str(), td::FileFd::Read);
    if (r_file.is_ok()) {
      return candidate;
    }
  }
  return td::CSlice();
}

}  // namespace

TEST(SslCtxIntegration, ExplicitSystemBundleVerifyOnProvidesUsableContext) {
  auto bundle = find_system_bundle();
  if (bundle.empty()) {
    return;
  }

  auto result = td::SslCtx::create(bundle, td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.ok().get_openssl_ctx() != nullptr);
}

TEST(SslCtxIntegration, ExplicitSystemBundleVerifyOffProvidesUsableContext) {
  auto bundle = find_system_bundle();
  if (bundle.empty()) {
    return;
  }

  auto result = td::SslCtx::create(bundle, td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.ok().get_openssl_ctx() != nullptr);
}

TEST(SslCtxIntegration, MissingExplicitBundleFailsForBothModesWithoutFallback) {
  auto on_result = td::SslCtx::create("/tmp/definitely-missing-ca-bundle.pem", td::SslCtx::VerifyPeer::On);
  auto off_result = td::SslCtx::create("/tmp/definitely-missing-ca-bundle.pem", td::SslCtx::VerifyPeer::Off);

  ASSERT_TRUE(on_result.is_error());
  ASSERT_TRUE(off_result.is_error());
}

TEST(SslCtxIntegration, VerifyOnAndVerifyOffContextsRemainIndependentWithExplicitBundle) {
  auto bundle = find_system_bundle();
  if (bundle.empty()) {
    return;
  }

  auto on_result = td::SslCtx::create(bundle, td::SslCtx::VerifyPeer::On);
  auto off_result = td::SslCtx::create(bundle, td::SslCtx::VerifyPeer::Off);

  ASSERT_TRUE(on_result.is_ok());
  ASSERT_TRUE(off_result.is_ok());
  ASSERT_TRUE(on_result.ok().get_openssl_ctx() != off_result.ok().get_openssl_ctx());
}

#endif
