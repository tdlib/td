//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include <cstdint>
#include <string>

namespace td {

struct TdParameters {
  bool use_test_dc = false;
  std::string database_directory;
  std::string files_directory;
  std::int32_t api_id = 0;
  std::string api_hash;
  bool use_file_db = true;
  bool enable_storage_optimizer = false;
  bool ignore_file_names = false;
  bool use_secret_chats = false;
  bool use_chat_info_db = false;
  bool use_message_db = false;
};

}  // namespace td
