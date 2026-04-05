//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/stealth/Interfaces.h"

namespace td {
namespace mtproto {
namespace test {

class MockRng final : public stealth::IRng {
 public:
  explicit MockRng(uint64 seed) {
    uint64 x = seed + 0x9e3779b97f4a7c15ULL;
    for (auto &v : state_) {
      x ^= x >> 30;
      x *= 0xbf58476d1ce4e5b9ULL;
      x ^= x >> 27;
      x *= 0x94d049bb133111ebULL;
      x ^= x >> 31;
      v = x;
    }
  }

  void fill_secure_bytes(MutableSlice dest) final {
    size_t i = 0;
    while (i < dest.size()) {
      auto v = next_u64();
      for (size_t j = 0; j < 8 && i < dest.size(); j++, i++) {
        dest[i] = static_cast<char>((v >> (j * 8)) & 0xff);
      }
    }
  }

  uint32 secure_uint32() final {
    return static_cast<uint32>(next_u64());
  }

  uint32 bounded(uint32 n) final {
    CHECK(n != 0);
    return secure_uint32() % n;
  }

 private:
  uint64 state_[4]{};

  uint64 next_u64() {
    uint64 result = rotl(state_[1] * 5, 7) * 9;
    uint64 t = state_[1] << 17;

    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];

    state_[2] ^= t;
    state_[3] = rotl(state_[3], 45);

    return result;
  }

  static uint64 rotl(uint64 x, int k) {
    return (x << k) | (x >> (64 - k));
  }
};

}  // namespace test
}  // namespace mtproto
}  // namespace td
