//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

namespace td {
namespace vault_detail {

inline constexpr unsigned long long kKeyRegistryDigestMain = 0xd1e2d80d6cd2992dULL;
inline constexpr unsigned long long kKeyRegistryDigestTest = 0x6c345fbd5cb7b08aULL;

}  // namespace vault_detail
}  // namespace td