//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"

namespace td {
namespace mtproto {
namespace test {
namespace fixtures {

constexpr uint16 kEchExtensionType = 0xFE0D;
constexpr uint16 kAlpsChrome131 = 0x4469;
constexpr uint16 kAlpsChrome133Plus = 0x44CD;
constexpr uint16 kX25519Group = 0x001D;
constexpr uint16 kPqHybridGroup = 0x11EC;

}  // namespace fixtures
}  // namespace test
}  // namespace mtproto
}  // namespace td
