//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileGcParameters.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/Global.h"

namespace td {
FileGcParameters::FileGcParameters(int64 size, int32 ttl, int32 count, int32 immunity_delay,
                                   vector<FileType> file_types, vector<DialogId> owner_dialog_ids,
                                   vector<DialogId> exclude_owner_dialog_ids, int32 dialog_limit)
    : file_types(std::move(file_types))
    , owner_dialog_ids(std::move(owner_dialog_ids))
    , exclude_owner_dialog_ids(std::move(exclude_owner_dialog_ids))
    , dialog_limit(dialog_limit) {
  auto &config = G()->shared_config();
  this->max_files_size =
      size >= 0 ? size : static_cast<int64>(config.get_option_integer("storage_max_files_size", 100 << 10)) << 10;

  this->max_time_from_last_access =
      ttl >= 0 ? ttl : config.get_option_integer("storage_max_time_from_last_access", 60 * 60 * 23);

  this->max_file_count = count >= 0 ? count : config.get_option_integer("storage_max_file_count", 40000);

  this->immunity_delay =
      immunity_delay >= 0 ? immunity_delay : config.get_option_integer("storage_immunity_delay", 60 * 60);
}
}  // namespace td
