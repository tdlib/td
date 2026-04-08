// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

namespace td {
namespace mtproto {
namespace test {
namespace fixtures {

constexpr const char kKnownTelegramJa3[] = "e0e58235789a753608b12649376e91ec";
constexpr uint16 kEchExtensionType = 0xFE0D;
constexpr uint16 kAlpsChrome131 = 0x4469;
constexpr uint16 kAlpsChrome133Plus = 0x44CD;
constexpr uint16 kTlsRsaWith3DesEdeCbcSha = 0x000A;
constexpr uint16 kTlsEcdheEcdsaWith3DesEdeCbcSha = 0xC008;
constexpr uint16 kTlsEcdheRsaWith3DesEdeCbcSha = 0xC012;
constexpr uint16 kX25519Group = 0x001D;
// Reserved for future Chrome120_PQ fixture coverage. Current runtime Chrome120 stays non-PQ.
constexpr uint16 kPqHybridDraftGroup = 0x6399;
constexpr uint16 kPqHybridGroup = 0x11EC;
constexpr uint16 kX25519KeyShareLength = 32;
constexpr uint16 kPqHybridKeyShareLength = 0x04C0;

}  // namespace fixtures
}  // namespace test
}  // namespace mtproto
}  // namespace td
