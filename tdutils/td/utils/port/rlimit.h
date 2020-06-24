//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

enum class ResourceLimitType { NoFile, Rss };

td::Status set_resource_limit(ResourceLimitType rlim_type, td::uint64 value, td::uint64 cap = 0);
td::Status set_maximize_resource_limit(ResourceLimitType rlim, td::uint64 value);
}  // namespace td
