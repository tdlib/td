//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/td_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class DownloadManager : public Actor {
 public:
  // creates, but do not stats the actor
  static td::unique_ptr<DownloadManager> create();

  struct Counters {
    int64 total_size{};
    int32 total_count{};
    int64 downloaded_size{};

    tl_object_ptr<td_api::updateFileDownloads> to_td_api() const;
  };

  struct FileDownload {
    FileId file_id;
    FileSourceId file_source_id;
    bool is_paused{};
    int32 add_date{};
    int32 complete_date{};
  };

  struct FoundFileDownloads {
    int32 total_count{};
    vector<FileDownload> file_downloads;
    string offset;
    tl_object_ptr<td_api::foundFileDownloads> to_td_api() const;
  };

  // Trying to make DownloadManager testable, so all interactions with G() will be hidden is this probably monstrous interface
  class Callback {
   public:
    virtual ~Callback() = default;
    virtual void update_counters(Counters counters) = 0;
    virtual void start_file(FileId file_id, int8 priority) = 0;
    virtual void pause_file(FileId file_id) = 0;
    virtual void delete_file(FileId file_id) = 0;
    virtual FileId dup_file_id(FileId file_id) = 0;

    virtual string get_unique_file_id(FileId file_id) = 0;
    virtual string get_file_source_serialized(FileSourceId file_source_id) = 0;
  };

  //
  // public interface for user
  //

  // sets callback to handle all updates
  virtual void set_callback(unique_ptr<Callback> callback) = 0;

  virtual Status toggle_is_paused(FileId, bool is_paused) = 0;
  virtual Status toggle_all_is_paused(bool is_paused) = 0;
  virtual Status remove_file(FileId file_id, FileSourceId file_source_id, bool delete_from_cache) = 0;
  virtual Status remove_all_files(bool only_active, bool only_completed, bool delete_from_cache) = 0;
  // Files are always added in is_paused = false state
  virtual Status add_file(FileId file_id, FileSourceId file_source_id, string search_by, int8 priority) = 0;
  virtual void search(std::string query, bool only_active, bool only_completed, string offset, int32 limit,
                      Promise<FoundFileDownloads> promise) = 0;

  //
  // private interface to handle all kinds of updates
  //
  virtual void update_file_download_state(FileId file_id, int64 download_size, int64 size, bool is_paused) = 0;
  virtual void update_file_deleted(FileId file_id) = 0;
};

}  // namespace td
