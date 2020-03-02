//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileStats.h"

#include "td/telegram/td_api.h"

#include "td/telegram/files/FileLoaderUtils.h"

#include "td/utils/common.h"
#include "td/utils/format.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace td {

tl_object_ptr<td_api::storageStatisticsFast> FileStatsFast::as_td_api() const {
  return make_tl_object<td_api::storageStatisticsFast>(size, count, database_size, language_pack_database_size,
                                                       log_size);
}

void FileStats::add(StatByType &by_type, FileType file_type, int64 size) {
  auto pos = static_cast<size_t>(file_type);
  CHECK(pos < stat_by_type.size());
  by_type[pos].size += size;
  by_type[pos].cnt++;
}

void FileStats::add_impl(const FullFileInfo &info) {
  if (split_by_owner_dialog_id) {
    add(stat_by_owner_dialog_id[info.owner_dialog_id], info.file_type, info.size);
  } else {
    add(stat_by_type, info.file_type, info.size);
  }
}

void FileStats::add_copy(const FullFileInfo &info) {
  add_impl(info);
  if (need_all_files) {
    all_files.push_back(info);
  }
}

void FileStats::add(FullFileInfo &&info) {
  add_impl(info);
  if (need_all_files) {
    all_files.push_back(std::move(info));
  }
}

FileTypeStat get_nontemp_stat(const FileStats::StatByType &by_type) {
  FileTypeStat stat;
  for (int32 i = 0; i < file_type_size; i++) {
    if (FileType(i) != FileType::Temp) {
      stat.size += by_type[i].size;
      stat.cnt += by_type[i].cnt;
    }
  }
  return stat;
}

FileTypeStat FileStats::get_total_nontemp_stat() const {
  if (!split_by_owner_dialog_id) {
    return get_nontemp_stat(stat_by_type);
  }
  FileTypeStat stat;
  for (auto &dialog : stat_by_owner_dialog_id) {
    auto tmp = get_nontemp_stat(dialog.second);
    stat.size += tmp.size;
    stat.cnt += tmp.cnt;
  }
  return stat;
}

void FileStats::apply_dialog_limit(int32 limit) {
  if (limit == -1) {
    return;
  }
  if (!split_by_owner_dialog_id) {
    return;
  }

  std::vector<std::pair<int64, DialogId>> dialogs;
  for (auto &dialog : stat_by_owner_dialog_id) {
    if (!dialog.first.is_valid()) {
      continue;
    }
    int64 size = 0;
    for (auto &it : dialog.second) {
      size += it.size;
    }
    dialogs.push_back(std::make_pair(size, dialog.first));
  }
  size_t prefix = dialogs.size();
  if (prefix > static_cast<size_t>(limit)) {
    prefix = static_cast<size_t>(limit);
  }
  std::partial_sort(dialogs.begin(), dialogs.begin() + prefix, dialogs.end(),
                    [](const auto &x, const auto &y) { return x.first > y.first; });
  dialogs.resize(prefix);

  std::unordered_set<DialogId, DialogIdHash> all_dialogs;

  for (auto &dialog : dialogs) {
    all_dialogs.insert(dialog.second);
  }

  StatByType other_stats;
  bool other_flag = false;
  for (auto it = stat_by_owner_dialog_id.begin(); it != stat_by_owner_dialog_id.end();) {
    if (all_dialogs.count(it->first)) {
      ++it;
    } else {
      for (int32 i = 0; i < file_type_size; i++) {
        other_stats[i].size += it->second[i].size;
        other_stats[i].cnt += it->second[i].cnt;
      }
      other_flag = true;
      it = stat_by_owner_dialog_id.erase(it);
    }
  }

  if (other_flag) {
    DialogId other_dialog_id;
    stat_by_owner_dialog_id[other_dialog_id] = other_stats;
  }
}

tl_object_ptr<td_api::storageStatisticsByChat> as_td_api(DialogId dialog_id,
                                                         const FileStats::StatByType &stat_by_type) {
  auto stats = make_tl_object<td_api::storageStatisticsByChat>(dialog_id.get(), 0, 0, Auto());
  int64 secure_raw_size = 0;
  int32 secure_raw_cnt = 0;
  int64 wallpaper_raw_size = 0;
  int32 wallpaper_raw_cnt = 0;
  for (int32 i = 0; i < file_type_size; i++) {
    FileType file_type = static_cast<FileType>(i);
    auto size = stat_by_type[i].size;
    auto cnt = stat_by_type[i].cnt;

    if (file_type == FileType::SecureRaw) {
      secure_raw_size = size;
      secure_raw_cnt = cnt;
      continue;
    }
    if (file_type == FileType::Wallpaper) {
      wallpaper_raw_size = size;
      wallpaper_raw_cnt = cnt;
      continue;
    }
    if (file_type == FileType::Secure) {
      size += secure_raw_size;
      cnt += secure_raw_cnt;
    }
    if (file_type == FileType::Background) {
      size += wallpaper_raw_size;
      cnt += wallpaper_raw_cnt;
    }
    if (size == 0) {
      continue;
    }

    stats->size_ += size;
    stats->count_ += cnt;
    stats->by_file_type_.push_back(
        make_tl_object<td_api::storageStatisticsByFileType>(as_td_api(file_type), size, cnt));
  }
  return stats;
}

tl_object_ptr<td_api::storageStatistics> FileStats::as_td_api() const {
  auto stats = make_tl_object<td_api::storageStatistics>(0, 0, Auto());
  if (!split_by_owner_dialog_id) {
    stats->by_chat_.reserve(1);
    stats->by_chat_.push_back(::td::as_td_api(DialogId(), stat_by_type));
  } else {
    stats->by_chat_.reserve(stat_by_owner_dialog_id.size());
    for (auto &by_dialog : stat_by_owner_dialog_id) {
      stats->by_chat_.push_back(::td::as_td_api(by_dialog.first, by_dialog.second));
    }
    std::sort(stats->by_chat_.begin(), stats->by_chat_.end(), [](const auto &x, const auto &y) {
      if (x->chat_id_ == 0 || y->chat_id_ == 0) {
        return (x->chat_id_ == 0) < (y->chat_id_ == 0);
      }
      return x->size_ > y->size_;
    });
  }
  for (const auto &by_dialog : stats->by_chat_) {
    stats->size_ += by_dialog->size_;
    stats->count_ += by_dialog->count_;
  }
  return stats;
}

std::vector<DialogId> FileStats::get_dialog_ids() const {
  std::vector<DialogId> res;
  if (!split_by_owner_dialog_id) {
    return res;
  }
  res.reserve(stat_by_owner_dialog_id.size());
  for (auto &by_dialog : stat_by_owner_dialog_id) {
    if (by_dialog.first.is_valid()) {
      res.push_back(by_dialog.first);
    }
  }
  return res;
}

StringBuilder &operator<<(StringBuilder &sb, const FileTypeStat &stat) {
  return sb << tag("size", format::as_size(stat.size)) << tag("count", stat.cnt);
}

StringBuilder &operator<<(StringBuilder &sb, const FileStats &file_stats) {
  if (!file_stats.split_by_owner_dialog_id) {
    FileTypeStat total_stat;
    for (auto &type_stat : file_stats.stat_by_type) {
      total_stat.size += type_stat.size;
      total_stat.cnt += type_stat.cnt;
    }

    sb << "[FileStat " << tag("total", total_stat);
    for (int32 i = 0; i < file_type_size; i++) {
      sb << tag(get_file_type_name(FileType(i)), file_stats.stat_by_type[i]);
    }
    sb << "]";
  } else {
    {
      FileTypeStat total_stat;
      for (auto &by_type : file_stats.stat_by_owner_dialog_id) {
        for (auto &type_stat : by_type.second) {
          total_stat.size += type_stat.size;
          total_stat.cnt += type_stat.cnt;
        }
      }
      sb << "[FileStat " << tag("total", total_stat);
    }
    for (auto &by_type : file_stats.stat_by_owner_dialog_id) {
      FileTypeStat dialog_stat;
      for (auto &type_stat : by_type.second) {
        dialog_stat.size += type_stat.size;
        dialog_stat.cnt += type_stat.cnt;
      }

      sb << "[FileStat " << tag("owner_dialog_id", by_type.first) << tag("total", dialog_stat);
      for (int32 i = 0; i < file_type_size; i++) {
        sb << tag(get_file_type_name(FileType(i)), by_type.second[i]);
      }
      sb << "]";
    }
    sb << "]";
  }

  return sb;
}

}  // namespace td
