//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2017
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/td_log.h"

#include "td/telegram/Log.h"

void td_set_log_file_path(const char *path) {
  td::Log::set_file_path(path);
}

void td_set_log_verbosity_level(int new_verbosity_level) {
  td::Log::set_verbosity_level(new_verbosity_level);
}
