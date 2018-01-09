//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Log.h"

#include "td/utils/common.h"
#include "td/utils/FileLog.h"
#include "td/utils/logging.h"

#include <algorithm>

namespace td {

static FileLog file_log;
static TsLog ts_log(&file_log);
static int64 max_log_file_size = 10 << 20;

void Log::set_file_path(string file_path) {
  if (file_path.empty()) {
    log_interface = default_log_interface;
    return;
  }

  file_log.init(file_path, max_log_file_size);
  log_interface = &ts_log;
}

void Log::set_max_file_size(int64 max_file_size) {
  max_log_file_size = std::max(max_file_size, static_cast<int64>(0));
  file_log.set_rotate_threshold(max_log_file_size);
}

void Log::set_verbosity_level(int new_verbosity_level) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(FATAL) + new_verbosity_level);
}

}  // namespace td
