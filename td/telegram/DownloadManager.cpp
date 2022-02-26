//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DownloadManager.h"

#include "td/telegram/DownloadsDb.h"
#include "td/telegram/Global.h"
#include "td/telegram/TdDb.h"

#include "td/utils/FlatHashMap.h"

namespace td {

class DownloadManagerImpl final : public DownloadManager {
 public:
  void set_callback(unique_ptr<Callback> callback) final {
    callback_ = std::move(callback);
    loop();
  }

  Status toggle_is_paused(FileId file_id, bool is_paused) final {
    if (!callback_) {
      return Status::OK();
    }
    auto it = active_files_.find(file_id);
    if (it == active_files_.end()) {
      return Status::Error(400, "Can't find file");
    }

    auto &file_info = it->second;
    if (is_paused != file_info.is_paused) {
      file_info.is_paused = is_paused;
      if (is_paused) {
        callback_->pause_file(file_info.internal_file_id);
      } else {
        callback_->start_file(file_info.internal_file_id, file_info.priority);
      }
    }

    // TODO: update db

    return Status::OK();
  }

  Status toggle_all_is_paused(bool is_paused) final {
    if (!callback_) {
      return Status::Error("TODO: code and message`");
    }
    for (auto &it : active_files_) {
      toggle_is_paused(it.first, is_paused);
    }

    // TODO: update db
    return Status::OK();
  }

  Status remove_file(FileId file_id, FileSourceId file_source_id, bool delete_from_cache) final {
    if (!callback_) {
      return Status::Error("TODO: code and message`");
    }
    auto it = active_files_.find(file_id);
    if (it != active_files_.end() && (!file_source_id.is_valid() || file_source_id == it->second.file_source_id)) {
      auto &file_info = it->second;
      if (!file_info.is_paused) {
        callback_->pause_file(file_info.internal_file_id);
      }
      if (delete_from_cache) {
        callback_->delete_file(file_info.internal_file_id);
      }
      by_internal_file_id_.erase(file_info.internal_file_id);

      active_files_.erase(it);
    }
    // TODO: remove from db
    return Status::OK();
  }

  Status remove_all_files(bool only_active, bool only_completed, bool delete_from_cache) final {
    if (!callback_) {
      return Status::Error("TODO: code and message`");
    }
    if (!only_completed) {
      for (auto &it : active_files_) {
        FileInfo &file_info = it.second;
        if (!file_info.is_paused) {
          callback_->pause_file(file_info.internal_file_id);
        }
        if (delete_from_cache) {
          callback_->delete_file(file_info.internal_file_id);
        }
      }
      active_files_.clear();
    }

    // TODO: remove from db. should respect only_active
    // TODO: if delete_from_cache, should iterate all files in db
    return Status::OK();
  }

  Status add_file(FileId file_id, FileSourceId file_source_id, string search_by, int8 priority) final {
    if (!callback_) {
      return Status::Error("TODO: code and message`");
    }
    FileInfo file_info;
    file_info.internal_file_id = callback_->dup_file_id(file_id);
    file_info.file_source_id = file_source_id;
    file_info.is_paused = false;
    file_info.priority = priority;
    by_internal_file_id_[file_info.internal_file_id] = file_id;

    if (active_files_.count(file_id) == 0) {
      counters_.total_count++;
      callback_->update_counters(counters_);
    }
    active_files_[file_id] = file_info;
    callback_->start_file(file_info.internal_file_id, file_info.priority);

    G()->td_db()->get_downloads_db_async()->add_download(
        DownloadsDbDownload{callback_->get_unique_file_id(file_id),
                            callback_->get_file_source_serialized(file_source_id), search_by, 0, priority},
        [](Result<Unit>) {});
  }

  void search(string query, bool only_active, bool only_completed, string offset, int32 limit,
              Promise<FoundFileDownloads> promise) final {
    if (!callback_) {
      return promise.set_error(Status::Error("TODO: code and message`"));
    }
    TRY_RESULT_PROMISE(promise, offset_int64, to_integer_safe<int64>(offset));
    // TODO: only active, only completed
    G()->td_db()->get_downloads_db_async()->get_downloads_fts(DownloadsDbFtsQuery{query, offset_int64, limit},
                                                              [](Result<DownloadsDbFtsResult>) {});
    return promise.set_value({});
  }

  void update_file_download_state(FileId internal_file_id, int64 download_size, int64 size, bool is_paused) final {
    if (!callback_) {
      return;
    }

    auto by_internal_file_id_it = by_internal_file_id_.find(internal_file_id);
    if (by_internal_file_id_it == by_internal_file_id_.end()) {
      return;
    }
    auto it = active_files_.find(by_internal_file_id_it->second);
    CHECK(it != active_files_.end());
    auto &file_info = it->second;
    counters_.downloaded_size -= file_info.downloaded_size;
    counters_.total_size -= file_info.size;
    file_info.size = size;
    file_info.downloaded_size = download_size;
    counters_.downloaded_size += file_info.downloaded_size;
    counters_.total_size += file_info.size;
    file_info.is_paused = is_paused;

    if (download_size == size) {
      active_files_.erase(it);
      by_internal_file_id_.erase(by_internal_file_id_it);

      if (active_files_.empty()) {
        counters_ = {};
      }
    }
    callback_->update_counters(counters_);
  }

  void update_file_deleted(FileId internal_file_id) final {
    if (!callback_) {
      return;
    }

    auto it = by_internal_file_id_.find(internal_file_id);
    if (it == by_internal_file_id_.end()) {
      return;
    }
    remove_file(it->second, {}, false);
  }

 private:
  unique_ptr<Callback> callback_;
  struct FileInfo {
    int8 priority;
    bool is_paused{};
    FileId internal_file_id{};
    FileSourceId file_source_id{};

    int64 size{};
    int64 downloaded_size{};
  };
  FlatHashMap<FileId, FileInfo, FileIdHash> active_files_;
  FlatHashMap<FileId, FileId, FileIdHash> by_internal_file_id_;

  Counters counters_;

  void loop() final {
    if (!callback_) {
      return;
    }
    // TODO: ???
    // TODO: load active files from db
    auto downloads = G()->td_db()->get_downloads_db_sync()->get_active_downloads().move_as_ok();
    for (auto &download : downloads.downloads) {
      // ...
    }
  }
  void tear_down() final {
    callback_.reset();
  }
};

unique_ptr<DownloadManager> DownloadManager::create() {
  return make_unique<DownloadManagerImpl>();
}

tl_object_ptr<td_api::foundFileDownloads> DownloadManager::FoundFileDownloads::to_td_api() const {
  return make_tl_object<td_api::foundFileDownloads>();
}
tl_object_ptr<td_api::updateFileDownloads> DownloadManager::Counters::to_td_api() const {
  return make_tl_object<td_api::updateFileDownloads>(total_size, total_count, downloaded_size);
}
}  // namespace td
