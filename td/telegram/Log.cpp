//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2017
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Log.h"

#include "td/utils/FileLog.h"
#include "td/utils/logging.h"

namespace td {
void Log::set_file_path(string path) {
  if (path.empty()) {
    log_interface = default_log_interface;
    return;
  }

  static FileLog file_log;
  static TsLog ts_log(&file_log);
  file_log.init(path);
  log_interface = &ts_log;
}

void Log::set_verbosity_level(int new_verbosity_level) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(FATAL) + new_verbosity_level);
}
}  // namespace td
