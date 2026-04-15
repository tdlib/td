//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

namespace td {
namespace vault_detail {

inline constexpr unsigned long long kHandshakeExpectedSetMain = 0xfb08be27ce31ac6cULL;
inline constexpr unsigned long long kHandshakeExpectedSetTest = 0xd2d7864e90fd7eedULL;

}  // namespace vault_detail
}  // namespace td