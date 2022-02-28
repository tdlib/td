//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DownloadManager.h"

#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileSourceId.hpp"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/TdDb.h"

#include "td/actor/MultiPromise.h"

#include "td/utils/algorithm.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Hints.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/tl_helpers.h"

#include <algorithm>
#include <functional>
#include <limits>

namespace td {

struct FileDownloadInDb {
  int64 download_id{};
  string unique_file_id;
  FileSourceId file_source_id;
  int32 priority{};
  int32 created_at{};
  int32 completed_at{};
  bool is_paused{};

  template <class StorerT>
  void store(StorerT &storer) const {
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_paused);
    END_STORE_FLAGS();
    td::store(download_id, storer);
    td::store(unique_file_id, storer);
    td::store(file_source_id, storer);
    td::store(priority, storer);
    td::store(created_at, storer);
    td::store(completed_at, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_paused);
    END_PARSE_FLAGS();
    td::parse(download_id, parser);
    td::parse(unique_file_id, parser);
    td::parse(file_source_id, parser);
    td::parse(priority, parser);
    td::parse(created_at, parser);
    td::parse(completed_at, parser);
  }
};

class DownloadManagerImpl final : public DownloadManager {
 public:
  void set_callback(unique_ptr<Callback> callback) final {
    callback_ = std::move(callback);
    loop();
  }

  Status toggle_is_paused(FileId file_id, bool is_paused) final {
    TRY_STATUS(check_is_active());
    TRY_RESULT(file_info_ptr, get_file_info(file_id));
    toggle_is_paused(*file_info_ptr, is_paused);
    return Status::OK();
  }

  Status toggle_all_is_paused(bool is_paused) final {
    TRY_STATUS(check_is_active());

    for (auto &it : files_) {
      toggle_is_paused(it.second, is_paused);
    }

    return Status::OK();
  }

  Status remove_file(FileId file_id, FileSourceId file_source_id, bool delete_from_cache) final {
    TRY_STATUS(check_is_active());
    TRY_RESULT(file_info_ptr, get_file_info(file_id, file_source_id));
    auto &file_info = *file_info_ptr;
    if (!file_info.is_paused) {
      callback_->pause_file(file_info.internal_file_id);
    }
    unregister_file_info(file_info);
    if (delete_from_cache) {
      callback_->delete_file(file_info.internal_file_id);
    }
    by_internal_file_id_.erase(file_info.internal_file_id);
    by_file_id_.erase(file_info.file_id);
    hints_.remove(file_info.download_id);

    remove_from_db(file_info);
    files_.erase(file_info.download_id);
    return Status::OK();
  }

  Status change_search_text(FileId file_id, FileSourceId file_source_id, string search_by) final {
    TRY_STATUS(check_is_active());
    TRY_RESULT(file_info_ptr, get_file_info(file_id, file_source_id));
    auto &file_info = *file_info_ptr;
    hints_.add(file_info.download_id, search_by);
    return Status::OK();
  }

  Status remove_all_files(bool only_active, bool only_completed, bool delete_from_cache) final {
    TRY_STATUS(check_is_active());
    vector<FileId> to_remove;
    for (auto &it : files_) {
      FileInfo &file_info = it.second;
      if (only_active && !is_active(file_info)) {
        continue;
      }
      if (only_completed && is_active(file_info)) {
        continue;
      }
      to_remove.push_back(file_info.file_id);
    }
    for (auto file_id : to_remove) {
      remove_file(file_id, {}, delete_from_cache);
    }
    return Status::OK();
  }

  Status add_file(FileId file_id, FileSourceId file_source_id, string search_by, int8 priority) final {
    TRY_STATUS(check_is_active());

    remove_file(file_id, {}, false);

    auto download_id = next_download_id();

    FileInfo file_info;
    file_info.download_id = download_id;
    file_info.file_id = file_id;
    file_info.internal_file_id = callback_->dup_file_id(file_id);
    file_info.file_source_id = file_source_id;
    file_info.is_paused = false;
    file_info.priority = priority;
    file_info.created_at = G()->unix_time();
    file_info.need_save_to_db = true;
    by_internal_file_id_[file_info.internal_file_id] = download_id;
    by_file_id_[file_info.file_id] = download_id;
    hints_.add(download_id, search_by);

    auto it = files_.emplace(download_id, std::move(file_info)).first;
    register_file_info(it->second);
    callback_->start_file(it->second.internal_file_id, it->second.priority);
    return Status::OK();
  }

  void search(string query, bool only_active, bool only_completed, string offset, int32 limit,
              Promise<td_api::object_ptr<td_api::foundFileDownloads>> promise) final {
    TRY_STATUS_PROMISE(promise, check_is_active());
    if (limit <= 0) {
      return promise.set_error(Status::Error(400, "Limit must be positive"));
    }
    int64 offset_int64 = std::numeric_limits<int64>::max();
    if (!offset.empty()) {
      auto r_offset = to_integer_safe<int64>(offset);
      if (r_offset.is_error()) {
        return promise.set_error(Status::Error(400, "Invalid offset"));
      }
      offset_int64 = r_offset.move_as_ok();
    }
    auto ids =
        query.empty() ? transform(files_, [](auto &it) { return it.first; }) : hints_.search(query, 10000).second;
    td::remove_if(ids, [&](auto download_id) {
      auto r = get_file_info(download_id);
      CHECK(r.is_ok());
      auto &file_info = *r.ok();
      if (only_active && !is_active(file_info)) {
        return true;
      }
      if (only_completed && is_active(file_info)) {
        return true;
      }
      if (download_id >= offset_int64) {
        return true;
      }
      return false;
    });
    std::sort(ids.begin(), ids.end(), std::greater<>());
    auto total_count = narrow_cast<int32>(ids.size());
    if (total_count > limit) {
      ids.resize(limit);
    }
    auto file_downloads = transform(ids, [&](auto id) {
      FileInfo &file_info = files_[id];
      return callback_->get_file_download_object(file_info.file_id, file_info.file_source_id, file_info.created_at,
                                                 file_info.completed_at, file_info.is_paused);
    });
    td::remove_if(file_downloads, [](const auto &file_download) { return file_download->message_ == nullptr; });
    string next_offset;
    if (!ids.empty()) {
      next_offset = to_string(ids.back());
    }
    promise.set_value(
        td_api::make_object<td_api::foundFileDownloads>(total_count, std::move(file_downloads), next_offset));
  }

  void update_file_download_state(FileId internal_file_id, int64 download_size, int64 size, bool is_paused) final {
    if (!callback_) {
      return;
    }
    auto r_file_info_ptr = get_file_info_by_internal(internal_file_id);
    if (r_file_info_ptr.is_error()) {
      return;
    }
    auto &file_info = *r_file_info_ptr.ok();

    with_file_info(file_info, [&](FileInfo &file_info) {
      file_info.size = size;
      file_info.downloaded_size = download_size;
      if (file_info.is_paused != is_paused) {
        file_info.is_paused = is_paused;
        file_info.need_save_to_db = true;
      }
    });
  }

  void update_file_deleted(FileId internal_file_id) final {
    if (!callback_) {
      return;
    }

    auto r_file_info_ptr = get_file_info_by_internal(internal_file_id);
    if (r_file_info_ptr.is_error()) {
      return;
    }
    auto &file_info = *r_file_info_ptr.ok();
    remove_file(file_info.file_id, {}, false);
  }

 private:
  unique_ptr<Callback> callback_;
  struct FileInfo {
    int64 download_id{};
    FileId file_id;
    FileId internal_file_id;
    FileSourceId file_source_id;
    int8 priority;
    bool is_paused{};
    mutable bool need_save_to_db{};
    int64 size{};
    int64 downloaded_size{};
    int32 created_at{};
    int32 completed_at{};
  };

  FlatHashMap<FileId, int64, FileIdHash> by_file_id_;
  FlatHashMap<FileId, int64, FileIdHash> by_internal_file_id_;
  FlatHashMap<int64, FileInfo> files_;
  // size_t active_file_count_{};
  Hints hints_;

  Counters counters_;
  Counters sent_counters_;
  bool is_synchonized_{false};
  bool is_started_{false};
  int64 max_download_id_{0};

  int64 next_download_id() {
    return ++max_download_id_;
  }

  static bool is_active(const FileInfo &file_info) {
    return file_info.size == 0 || file_info.downloaded_size != file_info.size;
  }

  static string pmc_key(const FileInfo &file_info) {
    return PSTRING() << "dlds#" << file_info.download_id;
  }
  void sync_with_db(const FileInfo &file_info) {
    if (!file_info.need_save_to_db) {
      return;
    }
    file_info.need_save_to_db = false;

    FileDownloadInDb to_save;
    to_save.download_id = file_info.download_id;
    to_save.file_source_id = file_info.file_source_id;
    to_save.is_paused = file_info.is_paused;
    to_save.priority = file_info.priority;
    to_save.created_at = file_info.created_at;
    to_save.completed_at = file_info.completed_at;
    to_save.unique_file_id = callback_->get_unique_file_id(file_info.file_id);
    G()->td_db()->get_binlog_pmc()->set(pmc_key(file_info), log_event_store(to_save).as_slice().str());
  }
  static void remove_from_db(const FileInfo &file_info) {
    G()->td_db()->get_binlog_pmc()->erase(pmc_key(file_info));
  }

  void on_synchronized(Result<Unit> result) {
    LOG(INFO) << "DownloadManager: synchronized";
    is_synchonized_ = true;
    update_counters();
  }

  void try_start() {
    if (is_started_) {
      return;
    }
    is_started_ = true;
    auto serialized_counter = G()->td_db()->get_binlog_pmc()->get("dlds_counter");
    if (!serialized_counter.empty()) {
      log_event_parse(sent_counters_, serialized_counter).ensure();
      callback_->update_counters(sent_counters_);
    }

    auto downloads_in_kv = G()->td_db()->get_binlog_pmc()->prefix_get("dlds#");
    MultiPromiseActorSafe mpas("DownloadManager: initialization");
    mpas.add_promise(promise_send_closure(actor_id(this), &DownloadManagerImpl::on_synchronized));
    mpas.set_ignore_errors(true);
    auto lock = mpas.get_promise();
    for (auto &it : downloads_in_kv) {
      Slice key = it.first;
      Slice value = it.second;
      FileDownloadInDb in_db;
      log_event_parse(in_db, value).ensure();
      CHECK(in_db.download_id == to_integer_safe<int64>(key).ok());
      max_download_id_ = max(in_db.download_id, max_download_id_);
      auto promise = mpas.get_promise();
      auto unique_file_id = std::move(in_db.unique_file_id);
      auto file_source_id = in_db.file_source_id;
      auto new_promise = [actor_id = actor_id(this), in_db = std::move(in_db),
                          promise = std::move(promise)](Result<FileSearchInfo> res) mutable {
        if (res.is_error()) {
          return promise.set_value(Unit());
        }
        send_closure(actor_id, &DownloadManagerImpl::add_file_from_db, res.move_as_ok(), std::move(in_db),
                     std::move(promise));
      };
      send_closure(G()->file_reference_manager(), &FileReferenceManager::get_file_search_info, file_source_id,
                   std::move(unique_file_id), std::move(new_promise));
    }
    lock.set_value(Unit());
  }

  void add_file_from_db(FileSearchInfo file_search_info, FileDownloadInDb in_db, Promise<Unit> promise) {
    if (by_file_id_.count(file_search_info.file_id) != 0) {
      // file has already been added
      return promise.set_value(Unit());
    }

    auto download_id = in_db.download_id;
    FileInfo file_info;
    file_info.download_id = download_id;
    file_info.file_id = file_search_info.file_id;
    file_info.internal_file_id = callback_->dup_file_id(file_info.file_id);
    file_info.file_source_id = in_db.file_source_id;
    file_info.is_paused = in_db.is_paused;
    file_info.priority = narrow_cast<int8>(in_db.priority);
    file_info.completed_at = in_db.completed_at;
    file_info.created_at = in_db.created_at;
    by_internal_file_id_[file_info.internal_file_id] = download_id;
    by_file_id_[file_info.file_id] = download_id;
    hints_.add(download_id, file_search_info.search_text);

    if (file_info.completed_at > 0) {
      // file must be removed from the list if it isn't fully downloaded
    } else {
      auto it = files_.emplace(download_id, file_info).first;
      register_file_info(it->second);
      callback_->start_file(it->second.internal_file_id, it->second.priority);
      promise.set_value(Unit());
    }
  }

  void loop() final {
    if (!callback_) {
      return;
    }
    try_start();
  }

  void tear_down() final {
    callback_.reset();
  }

  void toggle_is_paused(const FileInfo &file_info, bool is_paused) {
    if (!is_active(file_info) || is_paused == file_info.is_paused) {
      return;
    }

    with_file_info(file_info, [&](auto &file_info) {
      file_info.is_paused = is_paused;
      file_info.need_save_to_db = true;
    });
    if (is_paused) {
      callback_->pause_file(file_info.internal_file_id);
    } else {
      callback_->start_file(file_info.internal_file_id, file_info.priority);
    }
  }

  void update_counters() {
    if (!is_synchonized_) {
      return;
    }
    if (counters_ == sent_counters_) {
      return;
    }
    sent_counters_ = counters_;
    callback_->update_counters(counters_);
  }

  Result<const FileInfo *> get_file_info(FileId file_id, FileSourceId file_source_id = {}) {
    auto it = by_file_id_.find(file_id);
    if (it == by_file_id_.end()) {
      return Status::Error(400, "Can't find file");
    }
    return get_file_info(it->second, file_source_id);
  }

  Result<const FileInfo *> get_file_info_by_internal(FileId file_id) {
    auto it = by_internal_file_id_.find(file_id);
    if (it == by_internal_file_id_.end()) {
      return Status::Error(400, "Can't find file");
    }
    return get_file_info(it->second);
  }

  Result<const FileInfo *> get_file_info(int64 download_id, FileSourceId file_source_id = {}) {
    auto it = files_.find(download_id);
    if (it == files_.end()) {
      return Status::Error(400, "Can't find file");
    }
    if (file_source_id.is_valid() && file_source_id != it->second.file_source_id) {
      return Status::Error(400, "Can't find file with such source");
    }
    return &it->second;
  }

  void unregister_file_info(const FileInfo &file_info) {
    // active_file_count_ -= is_active(file_info);
    counters_.downloaded_size -= file_info.downloaded_size;
    counters_.total_size -= max(file_info.downloaded_size, file_info.size);
    counters_.total_count--;
  }

  void register_file_info(FileInfo &file_info) {
    // active_file_count_ += is_active(file_info);
    counters_.downloaded_size += file_info.downloaded_size;
    counters_.total_size += max(file_info.downloaded_size, file_info.size);
    counters_.total_count++;
    if (!is_active(file_info) && file_info.completed_at == 0) {
      file_info.completed_at = G()->unix_time();
      file_info.need_save_to_db = true;
    }
    sync_with_db(file_info);
    update_counters();
  }

  template <class F>
  void with_file_info(const FileInfo &const_file_info, F &&f) {
    unregister_file_info(const_file_info);
    auto &file_info = const_cast<FileInfo &>(const_file_info);
    f(file_info);
    register_file_info(file_info);
  }

  Status check_is_active() {
    if (!callback_) {
      LOG(ERROR) << "DownloadManager wasn't initialized";
      return Status::Error(500, "DownloadManager isn't initialized");
    }
    return Status::OK();
  }
};

unique_ptr<DownloadManager> DownloadManager::create() {
  return make_unique<DownloadManagerImpl>();
}

td_api::object_ptr<td_api::updateFileDownloads> DownloadManager::Counters::get_update_file_downloads_object() const {
  return td_api::make_object<td_api::updateFileDownloads>(total_size, total_count, downloaded_size);
}

template <class StorerT>
void DownloadManager::Counters::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(total_size, storer);
  td::store(total_count, storer);
  td::store(downloaded_size, storer);
}

template <class ParserT>
void DownloadManager::Counters::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(total_size, parser);
  td::parse(total_count, parser);
  td::parse(downloaded_size, parser);
}

}  // namespace td
