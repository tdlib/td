// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/Interfaces.h"

#include "td/utils/common.h"

namespace td {
namespace mtproto {
namespace stealth {
namespace stealth_rng_internal {

inline uint32 bounded_secure_uint32(IRng &rng, uint32 n) {
  CHECK(n > 0);

  auto threshold = static_cast<uint32>(-n) % n;
  while (true) {
    auto value = rng.secure_uint32();
    if (value >= threshold) {
      return value % n;
    }
  }
}

}  // namespace stealth_rng_internal
}  // namespace stealth
}  // namespace mtproto
}  // namespace td
