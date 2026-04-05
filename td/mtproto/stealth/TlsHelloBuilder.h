//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"

namespace td {
namespace mtproto {
namespace stealth {

string build_default_tls_client_hello(string domain, Slice secret, int32 unix_time);

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
