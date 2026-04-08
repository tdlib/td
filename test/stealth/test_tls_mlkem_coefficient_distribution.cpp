// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: ML-KEM-768 key material must look statistically
// indistinguishable from genuine ML-KEM keys. A DPI with access to the
// coefficient distribution could detect synthetic keys if bounded(3329)
// produces a biased distribution or if the 32-byte random tail is weak.

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <cmath>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

// Extract ML-KEM key share bytes from a PQ-capable ClientHello.
// ML-KEM-768 key is 1184 bytes: 384 coefficient pairs (3 bytes each) + 32 random bytes.
td::string extract_mlkem_key_share(td::Slice wire) {
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto hello = parsed.move_as_ok();
  for (const auto &entry : hello.key_share_entries) {
    if (entry.group == 0x11EC) {
      // PQ hybrid: ML-KEM-768 (1184 bytes) + X25519 (32 bytes)
      // ML-KEM portion is the first 1184 bytes of the key_data
      CHECK(entry.key_data.size() == 0x04C0);
      return entry.key_data.substr(0, 1184).str();
    }
  }
  return {};
}

// Decode NTT coefficients from ML-KEM-768 key bytes.
// Each 3 bytes encode two 12-bit coefficients.
td::vector<td::uint16> decode_mlkem_coefficients(td::Slice key) {
  CHECK(key.size() == 1152);  // 384 * 3 = 1152 (without trailing 32 random bytes)
  td::vector<td::uint16> coefficients;
  coefficients.reserve(768);
  for (size_t i = 0; i < key.size(); i += 3) {
    auto b0 = static_cast<td::uint8>(key[i]);
    auto b1 = static_cast<td::uint8>(key[i + 1]);
    auto b2 = static_cast<td::uint8>(key[i + 2]);
    auto a = static_cast<td::uint16>(b0 | ((b1 & 0x0F) << 8));
    auto b = static_cast<td::uint16>((b1 >> 4) | (b2 << 4));
    coefficients.push_back(a);
    coefficients.push_back(b);
  }
  return coefficients;
}

TEST(TlsMlkemCoefficientDistribution, AllCoefficientsMustBeWithinNttRange) {
  for (td::uint64 seed = 1; seed <= 50; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto mlkem_key = extract_mlkem_key_share(wire);
    ASSERT_FALSE(mlkem_key.empty());

    auto coefficients = decode_mlkem_coefficients(td::Slice(mlkem_key).substr(0, 1152));
    ASSERT_EQ(768u, coefficients.size());
    for (auto coeff : coefficients) {
      ASSERT_TRUE(coeff < 3329u);
    }
  }
}

TEST(TlsMlkemCoefficientDistribution, CoefficientsMustShowUniformDistribution) {
  // Chi-squared test: verify that coefficients are approximately uniformly
  // distributed in [0, 3329). A detectable bias would allow DPI to
  // distinguish synthetic keys from real ML-KEM public keys.
  td::vector<td::uint32> histogram(3329, 0);
  td::uint64 total_coefficients = 0;

  for (td::uint64 seed = 1; seed <= 100; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto mlkem_key = extract_mlkem_key_share(wire);
    if (mlkem_key.empty()) {
      continue;
    }
    auto coefficients = decode_mlkem_coefficients(td::Slice(mlkem_key).substr(0, 1152));
    for (auto coeff : coefficients) {
      histogram[coeff]++;
      total_coefficients++;
    }
  }

  ASSERT_TRUE(total_coefficients > 0u);
  double expected = static_cast<double>(total_coefficients) / 3329.0;

  // Chi-squared statistic
  double chi_squared = 0.0;
  for (auto count : histogram) {
    double diff = static_cast<double>(count) - expected;
    chi_squared += (diff * diff) / expected;
  }

  // For 3328 degrees of freedom, the critical value at p=0.001 is about 3527.
  // If chi-squared exceeds this, the distribution is detectably non-uniform.
  ASSERT_TRUE(chi_squared < 3600.0);
}

TEST(TlsMlkemCoefficientDistribution, RandomTailMustNotBeAllZeros) {
  // The last 32 bytes of the ML-KEM key are random bytes.
  // If they are all zeros, the key is obviously synthetic.
  for (td::uint64 seed = 1; seed <= 50; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto mlkem_key = extract_mlkem_key_share(wire);
    ASSERT_FALSE(mlkem_key.empty());
    ASSERT_EQ(1184u, mlkem_key.size());

    auto tail = td::Slice(mlkem_key).substr(1152, 32);
    bool all_zero = true;
    for (auto c : tail) {
      if (c != 0) {
        all_zero = false;
        break;
      }
    }
    ASSERT_FALSE(all_zero);
  }
}

TEST(TlsMlkemCoefficientDistribution, DifferentSeedsMustProduceDifferentKeys) {
  // If all seeds produce the same key, DPI can fingerprint it.
  std::unordered_set<td::string> unique_keys;
  for (td::uint64 seed = 1; seed <= 50; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto mlkem_key = extract_mlkem_key_share(wire);
    if (!mlkem_key.empty()) {
      unique_keys.insert(mlkem_key);
    }
  }
  // All 50 seeds should produce different ML-KEM keys.
  ASSERT_EQ(50u, unique_keys.size());
}

}  // namespace
