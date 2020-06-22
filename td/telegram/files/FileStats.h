//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileType.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

#include <array>
#include <unordered_map>

namespace td {
namespace td_api {
class storageStatistics;
class storageStatisticsFast;
}  // namespace td_api
}  // namespace td

namespace td {

struct FileTypeStat {
  int64 size{0};
  int32 cnt{0};
};

template <class StorerT>
void store(const FileTypeStat &stat, StorerT &storer) {
  using ::td::store;
  store(stat.size, storer);
  store(stat.cnt, storer);
}
template <class ParserT>
void parse(FileTypeStat &stat, ParserT &parser) {
  using ::td::parse;
  parse(stat.size, parser);
  parse(stat.cnt, parser);
}

struct FullFileInfo {
  FileType file_type;
  string path;
  DialogId owner_dialog_id;
  int64 size;
  uint64 atime_nsec;
  uint64 mtime_nsec;
};

struct FileStatsFast {
  int64 size{0};
  int32 count{0};
  int64 database_size{0};
  int64 language_pack_database_size{0};
  int64 log_size{0};
  FileStatsFast(int64 size, int32 count, int64 database_size, int64 language_pack_database_size, int64 log_size)
      : size(size)
      , count(count)
      , database_size(database_size)
      , language_pack_database_size(language_pack_database_size)
      , log_size(log_size) {
  }
  tl_object_ptr<td_api::storageStatisticsFast> get_storage_statistics_fast_object() const;
};

struct FileStats {
  bool need_all_files{false};
  bool split_by_owner_dialog_id{false};

  using StatByType = std::array<FileTypeStat, MAX_FILE_TYPE>;

  StatByType stat_by_type;
  std::unordered_map<DialogId, StatByType, DialogIdHash> stat_by_owner_dialog_id;

  std::vector<FullFileInfo> all_files;

  void add_copy(const FullFileInfo &info);
  void add(FullFileInfo &&info);
  void apply_dialog_limit(int32 limit);

  tl_object_ptr<td_api::storageStatistics> get_storage_statistics_object() const;
  std::vector<DialogId> get_dialog_ids() const;
  FileTypeStat get_total_nontemp_stat() const;

 private:
  void add_impl(const FullFileInfo &info);
  void add(StatByType &by_type, FileType file_type, int64 size);
};

StringBuilder &operator<<(StringBuilder &sb, const FileStats &file_stats);

}  // namespace td
