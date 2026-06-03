// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/crypto.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <array>
#include <cstring>

#if TD_HAVE_OPENSSL

TEST(RandomOpenSslInitIntegration, explicit_crypto_init_keeps_secure_bytes_available) {
  td::init_openssl_threads();
  td::init_crypto();

  std::array<unsigned char, 64> first{};
  std::array<unsigned char, 64> second{};

  td::Random::secure_cleanup();
  td::Random::secure_bytes(first.data(), first.size());
  td::Random::secure_cleanup();
  td::Random::secure_bytes(second.data(), second.size());

  ASSERT_TRUE(std::any_of(first.begin(), first.end(), [](unsigned char byte) { return byte != 0; }));
  ASSERT_TRUE(std::any_of(second.begin(), second.end(), [](unsigned char byte) { return byte != 0; }));
  ASSERT_NE(0, std::memcmp(first.data(), second.data(), first.size()));
}

#endif
