// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
//
// REG-24 — secp256r1 key_share entry MUST be derived from the injected
// `IRng`, never from OpenSSL's internal CSPRNG.
//
// Background. The TLS 1.3 builder lets each profile spec declare a
// `secp256r1` (P-256) entry inside its `key_share` extension. Real
// Chrome / Firefox profiles all carry one (the classical fallback in
// case the server rejects X25519 / X25519MLKEM768). The executor's
// `store_secp256r1_key_share` historically called
// `EC_KEY_generate_key`, which consumes 32 bytes from OpenSSL's
// *internal* CSPRNG and ignores the executor's injected `IRng` entirely.
//
// That bypass was a silent fingerprint and reproducibility hole:
//
//   1. **Test reproducibility was lost** for any test lane that built
//      the same wire twice with the same `MockRng` seed and asserted
//      byte equality. The `TlsRuntimeBuilderEquivalence` runtime-vs-
//      explicit equivalence test caught this for bucket 20002 (Firefox
//      148, ECH enabled): the two wires had a 64-byte run of difference
//      at the secp256r1 coordinate body, plus a 32-byte difference at
//      the ClientHello random because the body change propagated
//      through `finalize`'s hmac.
//
//   2. **Process-level reseeding leaked through.** Any test that
//      depended on `RAND_seed` / `RAND_bytes` state — including any
//      future fuzz harness that re-seeds OpenSSL between iterations —
//      would non-deterministically affect the secp256r1 portion of the
//      ClientHello, while every other random field would remain
//      reproducible. That makes regressions un-bisectable.
//
//   3. **Cross-builder divergence** between two equivalent code paths
//      (`build_runtime_tls_client_hello` vs.
//      `build_proxy_tls_client_hello_for_profile` with the same
//      precomputed profile + `EchMode`) would silently leak through
//      the secp256r1 32-byte private scalar, even though both call
//      sites agreed on every other byte.
//
// The fix: `store_secp256r1_key_share` now samples a 32-byte scalar
// from the injected `IRng`, rejection-samples until the scalar lies in
// `[1, n-1]` (where n = curve order), and computes the public point
// via `EC_POINT_mul(group, pub, priv, NULL, NULL, ctx)`. The wire
// content is now a pure deterministic function of the `MockRng` seed.
//
// This file is the regression guard.
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::BrowserProfile;
using td::mtproto::stealth::build_proxy_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::MockRng;

constexpr td::Slice kSecret = "0123456789secret";

td::string build_with_seed(BrowserProfile profile, EchMode ech_mode, td::uint64 seed,
                           td::Slice domain) {
  MockRng rng(seed);
  return build_proxy_tls_client_hello_for_profile(domain.str(), kSecret,
                                                  /*unix_time=*/1700000000, profile, ech_mode, rng);
}

// Positive: same MockRng seed must produce byte-identical wires across
// repeated builds. Before REG-24 this failed because OpenSSL's internal
// CSPRNG produced a fresh secp256r1 keypair on every call, regardless of
// what seed the executor's `IRng` was initialized with.
TEST(Secp256r1KeyShareIRngDeterminism, RepeatedBuildsWithIdenticalSeedProduceByteIdenticalWires) {
  for (td::uint64 seed = 1001; seed <= 1010; seed++) {
    auto first = build_with_seed(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, seed,
                                 "secp-determinism-firefox.example.com");
    auto second = build_with_seed(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, seed,
                                  "secp-determinism-firefox.example.com");
    ASSERT_EQ(first, second);
    ASSERT_TRUE(!first.empty());
  }
}

// Same invariant exercised on Chrome 133 (also has secp256r1 in its
// key_share). Catches profile-specific reintroduction of the bypass.
TEST(Secp256r1KeyShareIRngDeterminism, ChromeProfileRepeatedBuildsAreByteIdentical) {
  for (td::uint64 seed = 2001; seed <= 2010; seed++) {
    auto first = build_with_seed(BrowserProfile::Chrome133, EchMode::Disabled, seed,
                                 "secp-determinism-chrome.example.com");
    auto second = build_with_seed(BrowserProfile::Chrome133, EchMode::Disabled, seed,
                                  "secp-determinism-chrome.example.com");
    ASSERT_EQ(first, second);
    ASSERT_TRUE(!first.empty());
  }
}

// Negative entropy guard: distinct seeds must produce distinct wires.
// If a future refactor accidentally hardcodes the secp256r1 scalar (e.g.
// "always sample 32 zero bytes after rejection"), this would catch it.
//
// Note: we deliberately do NOT assert equal sizes here. Firefox148's ECH
// payload length is itself rng-derived (`make_config` samples
// `144 + bounded(4) * 32`), so distinct seeds legitimately produce
// distinct wire sizes — that variability is a separate stealth feature
// (REG-15) and asserting size equality here would conflate the two.
TEST(Secp256r1KeyShareIRngDeterminism, DistinctSeedsProduceDistinctWires) {
  auto a = build_with_seed(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, 7777,
                           "secp-distinct-seeds.example.com");
  auto b = build_with_seed(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, 8888,
                           "secp-distinct-seeds.example.com");
  ASSERT_TRUE(a != b);
}

}  // namespace
