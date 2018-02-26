//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Log.h"

#include "td/utils/port/CxCli.h"

namespace TdWindows {
using namespace CxCli;
public ref class Log sealed {
public:
  static void SetFilePath(String^ filePath) {
    ::td::Log::set_file_path(string_to_unmanaged(filePath));
  }

  static void SetMaxFileSize(std::int64_t maxFileSize) {
    ::td::Log::set_max_file_size(maxFileSize);
  }

  static void SetVerbosityLevel(int verbosityLevel) {
    ::td::Log::set_verbosity_level(verbosityLevel);
  }
};
}  // namespace TdWindows
