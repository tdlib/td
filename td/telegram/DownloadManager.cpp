//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DownloadManager.h"

#include "td/utils/FlatHashMap.h"
#include "td/utils/Hints.h"
#include "td/utils/tl_helpers.h"

#include "td/actor/MultiPromise.h"
#include "td/telegram/files/FileSourceId.hpp"
#include "td/telegram/DownloadsDb.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/TdDb.h"
#include "td/utils/algorithm.h"

#include "td/utils/FlatHashMap.h"

namespace td {

//TODO: replace LOG(ERROR) with something else
struct FileDownloadInDb {
  int64 download_id{};
  string unique_file_id;
  FileSourceId file_source_id;
  string search_text;
  bool is_paused{};
  int priority{};

  template <class StorerT>
  void store(StorerT &storer) const {
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_paused);
    END_STORE_FLAGS();
    td::store(download_id, storer);
    td::store(unique_file_id, storer);
    td::store(file_source_id, storer);
    td::store(priority, storer);
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
  }
};

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

    auto fit = by_file_id_.find(file_id);
    if (fit == by_file_id_.end()) {
      return Status::Error(400, "Can't find file");
    }
    auto it = files_.find(fit->second);
    CHECK(it != files_.end());

    auto &file_info = it->second;
    if (!is_active(file_info)) {
      return Status::Error(400, "File is already downloaded");
    }

    if (is_paused != file_info.is_paused) {
      file_info.is_paused = is_paused;
      if (is_paused) {
        callback_->pause_file(file_info.internal_file_id);
      } else {
        callback_->start_file(file_info.internal_file_id, file_info.priority);
      }
    }

    sync_with_db(file_info);

    return Status::OK();
  }

  Status toggle_all_is_paused(bool is_paused) final {
    if (!callback_) {
      return Status::Error("TODO: code and message`");
    }

    for (auto &it : files_) {
      toggle_is_paused(it.second.file_id, is_paused);
    }

    return Status::OK();
  }

  Status remove_file(FileId file_id, FileSourceId file_source_id, bool delete_from_cache) final {
    if (!callback_) {
      return Status::Error("TODO: code and message`");
    }
    auto fit = by_file_id_.find(file_id);
    if (fit == by_file_id_.end()) {
      return Status::OK();
    }
    auto it = files_.find(fit->second);
    if (it != files_.end() && (!file_source_id.is_valid() || file_source_id == it->second.file_source_id)) {
      auto &file_info = it->second;
      if (!file_info.is_paused) {
        callback_->pause_file(file_info.internal_file_id);
      }
      if (delete_from_cache) {
        callback_->delete_file(file_info.internal_file_id);
      }
      by_internal_file_id_.erase(file_info.internal_file_id);
      by_file_id_.erase(file_info.file_id);
      hints_.remove(file_info.download_id);
      counters_.total_count--;
      counters_.downloaded_size -= file_info.downloaded_size;
      counters_.total_size -= file_info.size;

      remove_from_db(file_info);
      files_.erase(it);
    }

    return Status::OK();
  }

  Status remove_all_files(bool only_active, bool only_completed, bool delete_from_cache) final {
    if (!callback_) {
      return Status::Error("TODO: code and message`");
    }
    //TODO(later): index?
    std::vector<FileId> to_remove;
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
    if (!callback_) {
      return Status::Error("TODO: code and message`");
    }

    // TODO: maybe we should ignore this query in some cases
    remove_file(file_id, {}, false);

    FileInfo file_info;
    file_info.download_id = next_download_id();
    file_info.file_id = file_id;
    file_info.internal_file_id = callback_->dup_file_id(file_id);
    file_info.file_source_id = file_source_id;
    file_info.is_paused = false;
    file_info.priority = priority;
    by_internal_file_id_[file_info.internal_file_id] = file_info.download_id;
    by_file_id_[file_info.file_id] = file_info.download_id;
    hints_.add(file_info.download_id, search_by);

    auto download_id = file_info.download_id;
    file_info.need_save_to_db = true;
    auto it = files_.emplace(download_id, file_info).first;
    callback_->start_file(it->second.internal_file_id, it->second.priority);
    sync_with_db(it->second);
    counters_.total_count++;
    update_counters();
    return Status::OK();
  }

  void search(string query, bool only_active, bool only_completed, string offset, int32 limit,
              Promise<FoundFileDownloads> promise) final {
    if (!callback_) {
      return promise.set_error(Status::Error("TODO: code and message`"));
    }
    TRY_RESULT_PROMISE(promise, offset_int64, to_integer_safe<int64>(offset));
    auto ids = hints_.search(query, 200, true).second;
    remove_if(ids, [&](auto download_id) {
      auto it = files_.find(download_id);
      if (it == files_.end()) {
        return true;
      }
      auto &file_info = it->second;
      if (only_active && !is_active(file_info)) {
        return true;
      }
      if (only_completed && is_active(file_info)) {
        return true;
      }
      return false;
    });
    std::sort(ids.begin(), ids.end(), std::greater<>());
    FoundFileDownloads found;
    found.total_count = narrow_cast<int32>(ids.size());
    remove_if(ids, [offset_int64](auto id) { return id < offset_int64; });
    if (static_cast<int32>(ids.size()) > limit) {
      ids.resize(max(0, limit));
    }
    int64 last_id = 0;
    found.file_downloads = transform(ids, [&](auto id) {
      auto it = files_.find(id);
      FileInfo &file_info = it->second;
      FileDownload res;
      res.is_paused = file_info.is_paused;
      res.file_source_id = file_info.file_source_id;
      res.file_id = it->second.file_id;
      return res;
    });
    if (last_id != 0) {
      found.offset = to_string(last_id);
    }
    return promise.set_value(std::move(found));
  }

  void update_file_download_state(FileId internal_file_id, int64 download_size, int64 size, bool is_paused) final {
    if (!callback_) {
      return;
    }

    auto by_internal_file_id_it = by_internal_file_id_.find(internal_file_id);
    if (by_internal_file_id_it == by_internal_file_id_.end()) {
      return;
    }
    auto it = files_.find(by_internal_file_id_it->second);
    CHECK(it != files_.end());
    auto &file_info = it->second;

    counters_.downloaded_size -= file_info.downloaded_size;
    counters_.total_size -= file_info.size;
    file_info.size = size;
    file_info.downloaded_size = download_size;
    counters_.downloaded_size += file_info.downloaded_size;
    counters_.total_size += file_info.size;
    file_info.is_paused = is_paused;

    update_counters();
  }

  void update_file_deleted(FileId internal_file_id) final {
    if (!callback_) {
      return;
    }

    auto fit = by_internal_file_id_.find(internal_file_id);
    if (fit == by_internal_file_id_.end()) {
      return;
    }
    auto it = files_.find(fit->second);
    CHECK(it != files_.end());
    remove_file(it->second.file_id, {}, false);
  }

 private:
  unique_ptr<Callback> callback_;
  struct FileInfo {
    int64 download_id{};
    int8 priority;
    bool is_paused{};
    FileId file_id{};
    FileId internal_file_id{};
    FileSourceId file_source_id{};
    int64 size{};
    int64 downloaded_size{};

    bool need_save_to_db{false};
  };

  FlatHashMap<FileId, int64, FileIdHash> by_file_id_;
  FlatHashMap<FileId, int64, FileIdHash> by_internal_file_id_;
  FlatHashMap<int64, FileInfo> files_;
  size_t active_files_count_{0};
  Hints hints_;

  Counters counters_;
  Counters sent_counters_;
  bool is_synchonized_{false};
  bool is_started_{false};
  int64 max_download_id_{0};

  int64 next_download_id() {
    auto res = ++max_download_id_;
    G()->td_db()->get_binlog_pmc()->set("dlds_max_id", to_string(res));
    return res;
  }

  bool is_active(const FileInfo &file_info) const {
    return file_info.size == 0 || file_info.downloaded_size != file_info.size;
  }
  static std::string pmc_key(const FileInfo &file_info) {
    return PSTRING() << "dlds" << file_info.download_id;
  }
  void sync_with_db(FileInfo &file_info) {
    if (!file_info.need_save_to_db) {
      return;
    }
    file_info.need_save_to_db = false;
    FileDownloadInDb to_save;
    to_save.download_id = file_info.download_id;
    to_save.file_source_id = file_info.file_source_id;
    to_save.is_paused = file_info.is_paused;
    to_save.priority = file_info.priority;
    to_save.unique_file_id = callback_->get_unique_file_id(file_info.file_id);
    G()->td_db()->get_binlog_pmc()->set(pmc_key(file_info), log_event_store(to_save).as_slice().str());
  }
  void remove_from_db(const FileInfo &file_info) {
    G()->td_db()->get_binlog_pmc()->erase(pmc_key(file_info));
  }

  void on_synchronized(Result<Unit>) {
    LOG(ERROR) << "DownloadManager: synchornized";
    is_synchonized_ = true;
    update_counters();
  }

  void try_start() {
    if (is_started_) {
      return;
    }
    is_started_ = true;
    auto serialized_counter = G()->td_db()->get_binlog_pmc()->get("dlds_counter");
    Counters counters_from_db;
    if (!serialized_counter.empty()) {
      log_event_parse(counters_from_db, serialized_counter).ensure();
    }
    callback_->update_counters(counters_from_db);

    max_download_id_ = to_integer<int64>(G()->td_db()->get_binlog_pmc()->get("dlds_max_id"));

    auto downloads_in_kv = G()->td_db()->get_binlog_pmc()->prefix_get("dlds#");
    MultiPromiseActorSafe mtps("DownloadManager: initialization");
    mtps.add_promise(promise_send_closure(actor_id(this), &DownloadManagerImpl::on_synchronized));
    for (auto &it : downloads_in_kv) {
      Slice key = it.first;
      Slice value = it.second;
      FileDownloadInDb in_db;
      log_event_parse(in_db, value).ensure();
      in_db.download_id = to_integer_safe<int64>(key.substr(4)).move_as_ok();
      auto promise = mtps.get_promise();
      // TODO: load data from MessagesManager
      auto unique_file_id = std::move(in_db.unique_file_id);
      auto file_source_id = in_db.file_source_id;
      auto new_promise = [self = actor_id(this), in_db = std::move(in_db),
                          promise = std::move(promise)](Result<FileId> res) mutable {
        if (res.is_error()) {
          promise.set_value({});
          return;
        }
        send_closure(self, &DownloadManagerImpl::add_file_from_db, res.move_as_ok(), std::move(in_db));
        promise.set_value({});
      };
      //      auto send_closure(G()->messages_manager(), &MessagesManager::todo, unique_file_id, file_source_id,
      //                        std::move(new_promise));
    }
  }

  void add_file_from_db(FileId file_id, FileDownloadInDb in_db) {
    FileInfo file_info;
    file_info.download_id = in_db.download_id;
    file_info.file_id = file_id;
    file_info.internal_file_id = callback_->dup_file_id(file_id);
    file_info.file_source_id = in_db.file_source_id;
    file_info.is_paused = false;
    file_info.priority = narrow_cast<int8>(in_db.priority);
    by_internal_file_id_[file_info.internal_file_id] = file_info.download_id;
    by_file_id_[file_info.file_id] = file_info.download_id;
    //TODO: hints_.add(file_info.download_id, search_by);

    auto download_id = file_info.download_id;
    auto it = files_.emplace(download_id, file_info).first;
    callback_->start_file(it->second.internal_file_id, it->second.priority);
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

  td::parse(total_count, parser);
  td::parse(downloaded_size, parser);
}
}  // namespace td
