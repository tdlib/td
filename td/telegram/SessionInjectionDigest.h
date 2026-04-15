//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

namespace td {
namespace vault_detail {

inline constexpr unsigned long long kSessionInjectionDigestMain = 0x718f9793901c2971ULL;
inline constexpr unsigned long long kSessionInjectionDigestTest = 0xffbf677736bfb43fULL;

}  // namespace vault_detail
}  // namespace td