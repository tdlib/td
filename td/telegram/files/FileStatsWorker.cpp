//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileStatsWorker.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileDb.h"
#include "td/telegram/files/FileLoaderUtils.h"
#include "td/telegram/Global.h"

#include "td/db/SqliteKeyValue.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/PathView.h"
#include "td/utils/port/path.h"
#include "td/utils/port/Stat.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"

#include <functional>
#include <unordered_map>

namespace td {
namespace {
// Performance ideas:
// - use slice instead of string
// - use arena memory allocator
// - store FileType or dir, no both
// - store dir relative to G()->files_dir()

struct DbFileInfo {
  FileType file_type;
  string path;
  DialogId owner_dialog_id;
  int64 size;
};

// long and blocking
template <class CallbackT>
Status scan_db(CallbackT &&callback) {
  G()->td_db()->get_file_db_shared()->pmc().get_by_range("file0", "file:", [&](Slice key, Slice value) {
    // skip reference to other data
    if (value.substr(0, 2) == "@@") {
      return;
    }
    FileData data;
    auto status = unserialize(data, value);
    if (status.is_error()) {
      LOG(ERROR) << "Invalid FileData in the database " << tag("value", format::escaped(value));
      return;
    }
    DbFileInfo info;
    if (data.local_.type() == LocalFileLocation::Type::Full) {
      info.file_type = data.local_.full().file_type_;
      info.path = data.local_.full().path_;
    } else if (data.local_.type() == LocalFileLocation::Type::Partial) {
      info.file_type = data.local_.partial().file_type_;
      info.path = data.local_.partial().path_;
    } else {
      return;
    }
    PathView path_view(info.path);
    if (path_view.is_relative()) {
      info.path = get_files_base_dir(info.file_type) + info.path;
    }
    // LOG(INFO) << "Found file in the database: " << data << " " << info.path;
    info.owner_dialog_id = data.owner_dialog_id_;
    info.size = data.size_;
    if (info.size == 0 && data.local_.type() == LocalFileLocation::Type::Full) {
      LOG(ERROR) << "Unknown size in the database";
      return;
    }
    callback(info);
  });
  return Status::OK();
}

struct FsFileInfo {
  FileType file_type;
  string path;
  int64 size;
  uint64 atime_nsec;
  uint64 mtime_nsec;
};

// long and blocking
template <class CallbackT>
Status scan_fs(CallbackT &&callback) {
  for (int i = 0; i < file_type_size; i++) {
    auto file_type = static_cast<FileType>(i);
    if (file_type == FileType::SecureRaw) {
      continue;
    }
    auto files_dir = get_files_dir(file_type);
    td::walk_path(files_dir, [&](CSlice path, bool is_dir) {
      if (is_dir) {
        // TODO: skip subdirs
        return;
      }
      auto r_stat = stat(path);
      if (r_stat.is_error()) {
        LOG(WARNING) << "Stat in files gc failed: " << r_stat.error();
        return;
      }
      auto stat = r_stat.move_as_ok();
      FsFileInfo info;
      info.path = path.str();
      info.size = stat.size_;
      info.file_type = file_type;
      info.atime_nsec = stat.atime_nsec_;
      info.mtime_nsec = stat.mtime_nsec_;
      callback(info);
    });
  }
  return Status::OK();
}
}  // namespace

void FileStatsWorker::get_stats(bool need_all_files, bool split_by_owner_dialog_id, Promise<FileStats> promise) {
  if (!G()->parameters().use_chat_info_db) {
    split_by_owner_dialog_id = false;
  }
  if (!split_by_owner_dialog_id) {
    FileStats file_stats;
    file_stats.need_all_files = need_all_files;
    auto start = Time::now();
    scan_fs([&](FsFileInfo &fs_info) {
      FullFileInfo info;
      info.file_type = fs_info.file_type;
      info.path = std::move(fs_info.path);
      info.size = fs_info.size;
      info.atime_nsec = fs_info.atime_nsec;
      info.mtime_nsec = fs_info.mtime_nsec;
      file_stats.add(std::move(info));
    });
    auto passed = Time::now() - start;
    LOG_IF(INFO, passed > 0.5) << "Get file stats took: " << format::as_time(passed);
    promise.set_value(std::move(file_stats));
  } else {
    auto start = Time::now();

    std::vector<FullFileInfo> full_infos;
    scan_fs([&](FsFileInfo &fs_info) {
      FullFileInfo info;
      info.file_type = fs_info.file_type;
      info.path = std::move(fs_info.path);
      info.size = fs_info.size;
      info.atime_nsec = fs_info.atime_nsec;
      info.mtime_nsec = fs_info.mtime_nsec;

      // LOG(INFO) << "Found file of size " << info.size << " at " << info.path;

      full_infos.push_back(std::move(info));
    });

    std::unordered_map<size_t, size_t> hash_to_pos;
    size_t pos = 0;
    for (auto &full_info : full_infos) {
      hash_to_pos[std::hash<std::string>()(full_info.path)] = pos;
      pos++;
    }
    scan_db([&](DbFileInfo &db_info) {
      auto it = hash_to_pos.find(std::hash<std::string>()(db_info.path));
      if (it == hash_to_pos.end()) {
        return;
      }
      // LOG(INFO) << "Match! " << db_info.path << " from " << db_info.owner_dialog_id;
      full_infos[it->second].owner_dialog_id = db_info.owner_dialog_id;
    });

    FileStats file_stats;
    file_stats.need_all_files = need_all_files;
    file_stats.split_by_owner_dialog_id = split_by_owner_dialog_id;
    for (auto &full_info : full_infos) {
      file_stats.add(std::move(full_info));
    }
    auto passed = Time::now() - start;
    LOG_IF(INFO, passed > 0.5) << "Get file stats took: " << format::as_time(passed);
    promise.set_value(std::move(file_stats));
  }
}

}  // namespace td
