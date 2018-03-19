//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileGcWorker.h"

#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/port/path.h"
#include "td/utils/Time.h"

#include <algorithm>
#include <array>

namespace td {
void FileGcWorker::do_remove_file(const FullFileInfo &info) {
  // LOG(WARNING) << "Gc remove file: " << tag("path", file) << tag("mtime", stat.mtime_nsec_ / 1000000000)
  // << tag("atime", stat.atime_nsec_ / 1000000000);
  // TODO: remove file from db too.
  auto status = unlink(info.path);
  LOG_IF(WARNING, status.is_error()) << "Failed to unlink file during files gc: " << status;
  send_closure(G()->file_manager(), &FileManager::on_file_unlink,
               FullLocalFileLocation(info.file_type, info.path, info.mtime_nsec));
}

void FileGcWorker::run_gc(const FileGcParameters &parameters, std::vector<FullFileInfo> files,
                          Promise<FileStats> promise) {
  auto begin_time = Time::now();
  LOG(INFO) << "Start files gc";
  // quite stupid implementations
  // needs a lot of memory
  // may write something more clever, but i will need at least 2 passes over the files
  // TODO update atime for all files in android (?)

  std::array<bool, file_type_size> immune_types{{false}};

  if (G()->parameters().use_file_db) {
    // immune by default
    immune_types[narrow_cast<size_t>(FileType::Sticker)] = true;
    immune_types[narrow_cast<size_t>(FileType::ProfilePhoto)] = true;
    immune_types[narrow_cast<size_t>(FileType::Thumbnail)] = true;
    immune_types[narrow_cast<size_t>(FileType::Wallpaper)] = true;
  }

  if (!parameters.file_types.empty()) {
    std::fill(immune_types.begin(), immune_types.end(), true);
    for (auto file_type : parameters.file_types) {
      immune_types[narrow_cast<size_t>(file_type)] = false;
    }
  }

  if (G()->parameters().use_file_db) {
    immune_types[narrow_cast<size_t>(FileType::EncryptedThumbnail)] = true;
  }

  auto file_cnt = files.size();
  int32 type_immunity_ignored_cnt = 0;
  int32 time_immunity_ignored_cnt = 0;
  int32 exclude_owner_dialog_id_ignored_cnt = 0;
  int32 owner_dialog_id_ignored_cnt = 0;
  int32 remove_by_atime_cnt = 0;
  int32 remove_by_count_cnt = 0;
  int32 remove_by_size_cnt = 0;
  int64 total_removed_size = 0;
  int64 total_size = 0;
  for (auto &info : files) {
    if (info.atime_nsec < info.mtime_nsec) {
      info.atime_nsec = info.mtime_nsec;
    }
    total_size += info.size;
  }

  FileStats new_stats;
  new_stats.split_by_owner_dialog_id = parameters.dialog_limit != 0;

  // Remove all files with atime > now - max_time_from_last_access
  double now = Clocks::system();
  files.erase(
      std::remove_if(
          files.begin(), files.end(),
          [&](const FullFileInfo &info) {
            if (immune_types[narrow_cast<size_t>(info.file_type)]) {
              type_immunity_ignored_cnt++;
              new_stats.add(FullFileInfo(info));
              return true;
            }
            if (std::find(parameters.exclude_owner_dialog_ids.begin(), parameters.exclude_owner_dialog_ids.end(),
                          info.owner_dialog_id) != parameters.exclude_owner_dialog_ids.end()) {
              exclude_owner_dialog_id_ignored_cnt++;
              new_stats.add(FullFileInfo(info));
              return true;
            }
            if (!parameters.owner_dialog_ids.empty() &&
                std::find(parameters.owner_dialog_ids.begin(), parameters.owner_dialog_ids.end(),
                          info.owner_dialog_id) == parameters.owner_dialog_ids.end()) {
              owner_dialog_id_ignored_cnt++;
              new_stats.add(FullFileInfo(info));
              return true;
            }
            if (static_cast<double>(info.mtime_nsec / 1000000000) > now - parameters.immunity_delay) {
              // new files are immune to gc.
              time_immunity_ignored_cnt++;
              new_stats.add(FullFileInfo(info));
              return true;
            }

            if (static_cast<double>(info.atime_nsec / 1000000000) < now - parameters.max_time_from_last_access) {
              do_remove_file(info);
              total_removed_size += info.size;
              remove_by_atime_cnt++;
              return true;
            }
            return false;
          }),
      files.end());

  // sort by max(atime, mtime)
  std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.atime_nsec < b.atime_nsec; });

  // 1. Total memory must be less than max_memory
  // 2. Total file count must be less than MAX_FILE_COUNT
  size_t remove_count = 0;
  if (files.size() > parameters.max_file_count) {
    remove_count = files.size() - parameters.max_file_count;
  }
  int64 remove_size = -parameters.max_files_size;
  for (auto &file : files) {
    remove_size += file.size;
  }

  size_t pos = 0;
  while (pos < files.size() && (remove_count > 0 || remove_size > 0)) {
    if (remove_count > 0) {
      remove_by_count_cnt++;
    } else {
      remove_by_size_cnt++;
    }

    if (remove_count > 0) {
      remove_count--;
    }
    remove_size -= files[pos].size;

    total_removed_size += files[pos].size;
    do_remove_file(files[pos]);
    pos++;
  }

  while (pos < files.size()) {
    new_stats.add(std::move(files[pos]));
    pos++;
  }

  auto end_time = Time::now();

  LOG(INFO) << "Finish files gc: " << tag("time", end_time - begin_time) << tag("total", file_cnt)
            << tag("removed", remove_by_atime_cnt + remove_by_count_cnt + remove_by_size_cnt)
            << tag("total_size", format::as_size(total_size))
            << tag("total_removed_size", format::as_size(total_removed_size)) << tag("by_atime", remove_by_atime_cnt)
            << tag("by_count", remove_by_count_cnt) << tag("by_size", remove_by_size_cnt)
            << tag("type_immunity", type_immunity_ignored_cnt) << tag("time_immunity", time_immunity_ignored_cnt)
            << tag("owner_dialog_id_immunity", owner_dialog_id_ignored_cnt)
            << tag("exclude_owner_dialog_id_immunity", exclude_owner_dialog_id_ignored_cnt);

  promise.set_value(std::move(new_stats));
}
}  // namespace td
