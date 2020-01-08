//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileEncryptionKey.h"
#include "td/telegram/files/FileLoader.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileType.h"

#include "td/utils/port/FileFd.h"
#include "td/utils/Status.h"
#include "td/utils/UInt.h"

#include <utility>

namespace td {

class FileUploader : public FileLoader {
 public:
  class Callback : public FileLoader::Callback {
   public:
    virtual void on_hash(string hash) = 0;
    virtual void on_partial_upload(const PartialRemoteFileLocation &partial_remote, int64 ready_size) = 0;
    virtual void on_ok(FileType file_type, const PartialRemoteFileLocation &partial_remote, int64 size) = 0;
    virtual void on_error(Status status) = 0;
  };

  FileUploader(const LocalFileLocation &local, const RemoteFileLocation &remote, int64 expected_size,
               const FileEncryptionKey &encryption_key, std::vector<int> bad_parts, unique_ptr<Callback> callback);

  // Should just implement all parent pure virtual methods.
  // Must not call any of them...
 private:
  ResourceState resource_state_;
  LocalFileLocation local_;
  RemoteFileLocation remote_;
  int64 expected_size_;
  FileEncryptionKey encryption_key_;
  std::vector<int> bad_parts_;
  unique_ptr<Callback> callback_;
  int64 local_size_ = 0;
  bool local_is_ready_ = false;
  FileType file_type_ = FileType::Temp;

  std::vector<UInt256> iv_map_;
  UInt256 iv_;
  string generate_iv_;
  int64 generate_offset_ = 0;
  int64 next_offset_ = 0;

  FileFd fd_;
  string fd_path_;
  bool is_temp_ = false;
  int64 file_id_;
  bool big_flag_;

  Result<FileInfo> init() override TD_WARN_UNUSED_RESULT;
  Status on_ok(int64 size) override TD_WARN_UNUSED_RESULT;
  void on_error(Status status) override;
  Status before_start_parts() override;
  void after_start_parts() override;
  Result<std::pair<NetQueryPtr, bool>> start_part(Part part, int32 part_count,
                                                  int64 streaming_offset) override TD_WARN_UNUSED_RESULT;
  Result<size_t> process_part(Part part, NetQueryPtr net_query) override TD_WARN_UNUSED_RESULT;
  void on_progress(Progress progress) override;
  FileLoader::Callback *get_callback() override;
  Result<PrefixInfo> on_update_local_location(const LocalFileLocation &location,
                                              int64 file_size) override TD_WARN_UNUSED_RESULT;

  Status generate_iv_map();

  bool keep_fd_ = false;
  void keep_fd_flag(bool keep_fd) override;
  void try_release_fd();
  Status acquire_fd() TD_WARN_UNUSED_RESULT;
};

}  // namespace td
