//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/telegram/files/FileEncryptionKey.h"
#include "td/telegram/files/FileLoader.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQuery.h"

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/Status.h"

#include <map>
#include <set>
#include <utility>

namespace td {
class FileDownloader : public FileLoader {
 public:
  class Callback : public FileLoader::Callback {
   public:
    virtual void on_start_download() = 0;
    virtual void on_partial_download(const PartialLocalFileLocation &partial_local, int64 ready_size, int64 size) = 0;
    virtual void on_ok(const FullLocalFileLocation &full_local, int64 size, bool is_new) = 0;
    virtual void on_error(Status status) = 0;
  };

  FileDownloader(const FullRemoteFileLocation &remote, const LocalFileLocation &local, int64 size, string name,
                 const FileEncryptionKey &encryption_key, bool is_small, bool search_file, int64 offset, int64 limit,
                 unique_ptr<Callback> callback);

  // Should just implement all parent pure virtual methods.
  // Must not call any of them...
 private:
  enum class QueryType : uint8 { Default = 1, CDN, ReuploadCDN };
  ResourceState resource_state_;
  FullRemoteFileLocation remote_;
  LocalFileLocation local_;
  int64 size_;
  string name_;
  FileEncryptionKey encryption_key_;
  unique_ptr<Callback> callback_;
  bool only_check_{false};

  string path_;
  FileFd fd_;

  int32 next_part_ = 0;
  bool next_part_stop_ = false;
  bool is_small_;
  bool search_file_{false};
  int64 offset_;
  int64 limit_;

  bool use_cdn_ = false;
  DcId cdn_dc_id_;
  string cdn_encryption_key_;
  string cdn_encryption_iv_;
  string cdn_file_token_;
  int32 cdn_file_token_generation_{0};
  std::map<int32, string> cdn_part_reupload_token_;
  std::map<int32, int32> cdn_part_file_token_generation_;

  bool need_check_{false};
  struct HashInfo {
    int64 offset;
    size_t size;
    string hash;
    bool operator<(const HashInfo &other) const {
      return offset < other.offset;
    }
  };
  std::set<HashInfo> hash_info_;
  bool has_hash_query_ = false;

  Result<FileInfo> init() override TD_WARN_UNUSED_RESULT;
  Status on_ok(int64 size) override TD_WARN_UNUSED_RESULT;
  void on_error(Status status) override;
  Result<bool> should_restart_part(Part part, NetQueryPtr &net_query) override TD_WARN_UNUSED_RESULT;
  Result<std::pair<NetQueryPtr, bool>> start_part(Part part, int32 part_count,
                                                  int64 streaming_offset) override TD_WARN_UNUSED_RESULT;
  Result<size_t> process_part(Part part, NetQueryPtr net_query) override TD_WARN_UNUSED_RESULT;
  void on_progress(Progress progress) override;
  FileLoader::Callback *get_callback() override;
  Status process_check_query(NetQueryPtr net_query) override;
  Result<CheckInfo> check_loop(int64 checked_prefix_size, int64 ready_prefix_size, bool is_ready) override;
  void add_hash_info(const std::vector<telegram_api::object_ptr<telegram_api::fileHash>> &hashes);

  bool keep_fd_ = false;
  void keep_fd_flag(bool keep_fd) override;
  void try_release_fd();
  Status acquire_fd() TD_WARN_UNUSED_RESULT;

  Status check_net_query(NetQueryPtr &net_query);
};
}  // namespace td
