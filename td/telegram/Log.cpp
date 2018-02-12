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
#include "td/utils/Slice.h"

namespace td {

static FileLog file_log;
static TsLog ts_log(&file_log);
static int64 max_log_file_size = 10 << 20;
static Log::FatalErrorCallbackPtr fatal_error_callback;

static void fatal_error_callback_wrapper(CSlice message) {
  CHECK(fatal_error_callback != nullptr);
  fatal_error_callback(message.c_str());
}

bool Log::set_file_path(string file_path) {
  if (file_path.empty()) {
    log_interface = default_log_interface;
    return true;
  }

  if (file_log.init(file_path, max_log_file_size)) {
    log_interface = &ts_log;
    return true;
  }

  return false;
}

void Log::set_max_file_size(int64 max_file_size) {
  max_log_file_size = max(max_file_size, static_cast<int64>(0));
  file_log.set_rotate_threshold(max_log_file_size);
}

void Log::set_verbosity_level(int new_verbosity_level) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(FATAL) + new_verbosity_level);
}

void Log::set_fatal_error_callback(FatalErrorCallbackPtr callback) {
  if (callback == nullptr) {
    fatal_error_callback = nullptr;
    set_log_fatal_error_callback(nullptr);
  } else {
    fatal_error_callback = callback;
    set_log_fatal_error_callback(fatal_error_callback_wrapper);
  }
}

}  // namespace td
