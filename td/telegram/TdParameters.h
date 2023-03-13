//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"

namespace td {

struct TdParameters {
  string database_directory;
  string files_directory;
  int32 api_id = 0;
  string api_hash;
  bool use_test_dc = false;
  bool use_file_db = false;
  bool use_chat_info_db = false;
  bool use_message_db = false;
  bool use_secret_chats = false;

  // TODO move to options
  bool enable_storage_optimizer = false;
  bool ignore_file_names = false;
};

}  // namespace td
