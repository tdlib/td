//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/GitInfo.h"

#include "auto/git_info.h"

#if !defined(GIT_COMMIT)
#define GIT_COMMIT "unknown"
#endif

#if !defined(GIT_DIRTY)
#define GIT_DIRTY false
#endif

namespace td {

const char *GitInfo::commit() {
  return GIT_COMMIT;
}

bool GitInfo::is_dirty() {
  return GIT_DIRTY;
}

}  // namespace td
