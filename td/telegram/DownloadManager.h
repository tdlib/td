//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#include "td/actor/actor.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/utils/common.h"

namespace td {
class DownloadManager : public td::Actor {
 public:
  // creates, but do not stats the actor
  static td::unique_ptr<DownloadManager> create();

  struct Counters {
    int64 total_size{};
    int32 total_count{};
    int64 downloaded_size{};
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
    std::vector<FileDownload> file_downloads;
    std::string offset;
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

    virtual std::string get_unique_file_id(FileId file_id) = 0;
    virtual std::string get_file_source_serialized(FileSourceId file_source_id) = 0;
  };

  //
  // public interface for user
  //

  // sets callback to handle all updates
  virtual void set_callback(unique_ptr<Callback> callback) = 0;

  virtual Status toggle_is_paused(FileId, bool is_paused) = 0;
  virtual void toggle_all_is_paused(bool is_paused) = 0;
  virtual void remove_file(FileId file_id, FileSourceId file_source_id, bool delete_from_cache) = 0;
  virtual void remove_all_files(bool only_active, bool only_completed, bool delete_from_cache) = 0;
  // Files are always added in is_paused = false state
  virtual void add_file(FileId file_id, FileSourceId file_source_id, std::string search_by, int8 priority) = 0;
  virtual FoundFileDownloads search(std::string query, bool only_active, bool only_completed, std::string offset,
                                    int32 limit) = 0;

  //
  // private interface to handle all kinds of updates
  //
  virtual void update_file_download_state(FileId file_id, int64 download_size, int64 size, bool is_paused) = 0;
  virtual void update_file_deleted(FileId file_id) = 0;
};
};  // namespace td
