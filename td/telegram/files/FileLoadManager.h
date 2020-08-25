//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/files/FileDownloader.h"
#include "td/telegram/files/FileEncryptionKey.h"
#include "td/telegram/files/FileFromBytes.h"
#include "td/telegram/files/FileHashUploader.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/files/FileUploader.h"
#include "td/telegram/files/ResourceManager.h"
#include "td/telegram/net/DcId.h"

#include "td/utils/buffer.h"
#include "td/utils/Container.h"
#include "td/utils/Status.h"

#include <map>

namespace td {

class FileLoadManager final : public Actor {
 public:
  using QueryId = uint64;
  class Callback : public Actor {
   public:
    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    ~Callback() override = default;
    virtual void on_start_download(QueryId id) = 0;
    virtual void on_partial_download(QueryId id, const PartialLocalFileLocation &partial_local, int64 ready_size,
                                     int64 size) = 0;
    virtual void on_partial_upload(QueryId id, const PartialRemoteFileLocation &partial_remote, int64 ready_size) = 0;
    virtual void on_hash(QueryId id, string hash) = 0;
    virtual void on_upload_ok(QueryId id, FileType file_type, const PartialRemoteFileLocation &remtoe, int64 size) = 0;
    virtual void on_upload_full_ok(QueryId id, const FullRemoteFileLocation &remote) = 0;
    virtual void on_download_ok(QueryId id, const FullLocalFileLocation &local, int64 size, bool is_new) = 0;
    virtual void on_error(QueryId id, Status status) = 0;
  };

  explicit FileLoadManager(ActorShared<Callback> callback, ActorShared<> parent);
  void download(QueryId id, const FullRemoteFileLocation &remote_location, const LocalFileLocation &local, int64 size,
                string name, const FileEncryptionKey &encryption_key, bool search_file, int64 offset, int64 limit,
                int8 priority);
  void upload(QueryId id, const LocalFileLocation &local_location, const RemoteFileLocation &remote_location,
              int64 expected_size, const FileEncryptionKey &encryption_key, int8 priority, vector<int> bad_parts);
  void upload_by_hash(QueryId id, const FullLocalFileLocation &local_location, int64 size, int8 priority);
  void update_priority(QueryId id, int8 priority);
  void from_bytes(QueryId id, FileType type, BufferSlice bytes, string name);
  void cancel(QueryId id);
  void update_local_file_location(QueryId id, const LocalFileLocation &local);
  void update_downloaded_part(QueryId id, int64 offset, int64 limit);
  void get_content(const FullLocalFileLocation &local_location, Promise<BufferSlice> promise);

 private:
  struct Node {
    QueryId query_id_;
    ActorOwn<FileLoaderActor> loader_;
    ResourceState resource_state_;
  };
  using NodeId = uint64;

  std::map<DcId, ActorOwn<ResourceManager>> download_resource_manager_map_;
  std::map<DcId, ActorOwn<ResourceManager>> download_small_resource_manager_map_;
  ActorOwn<ResourceManager> upload_resource_manager_;

  Container<Node> nodes_container_;
  ActorShared<Callback> callback_;
  ActorShared<> parent_;
  std::map<QueryId, NodeId> query_id_to_node_id_;
  bool stop_flag_ = false;

  void start_up() override;
  void loop() override;
  void hangup() override;
  void hangup_shared() override;

  void close_node(NodeId node_id);
  ActorOwn<ResourceManager> &get_download_resource_manager(bool is_small, DcId dc_id);

  void on_start_download();
  void on_partial_download(const PartialLocalFileLocation &partial_local, int64 ready_size, int64 size);
  void on_partial_upload(const PartialRemoteFileLocation &partial_remote, int64 ready_size);
  void on_hash(string hash);
  void on_ok_download(const FullLocalFileLocation &local, int64 size, bool is_new);
  void on_ok_upload(FileType file_type, const PartialRemoteFileLocation &remote, int64 size);
  void on_ok_upload_full(const FullRemoteFileLocation &remote);
  void on_error(Status status);
  void on_error_impl(NodeId node_id, Status status);

  class FileDownloaderCallback : public FileDownloader::Callback {
   public:
    explicit FileDownloaderCallback(ActorShared<FileLoadManager> actor_id) : actor_id_(std::move(actor_id)) {
    }

   private:
    ActorShared<FileLoadManager> actor_id_;

    void on_start_download() override {
      send_closure(actor_id_, &FileLoadManager::on_start_download);
    }
    void on_partial_download(const PartialLocalFileLocation &partial_local, int64 ready_size, int64 size) override {
      send_closure(actor_id_, &FileLoadManager::on_partial_download, partial_local, ready_size, size);
    }
    void on_ok(const FullLocalFileLocation &full_local, int64 size, bool is_new) override {
      send_closure(std::move(actor_id_), &FileLoadManager::on_ok_download, full_local, size, is_new);
    }
    void on_error(Status status) override {
      send_closure(std::move(actor_id_), &FileLoadManager::on_error, std::move(status));
    }
  };

  class FileUploaderCallback : public FileUploader::Callback {
   public:
    explicit FileUploaderCallback(ActorShared<FileLoadManager> actor_id) : actor_id_(std::move(actor_id)) {
    }

   private:
    ActorShared<FileLoadManager> actor_id_;

    void on_hash(string hash) override {
      send_closure(actor_id_, &FileLoadManager::on_hash, std::move(hash));
    }
    void on_partial_upload(const PartialRemoteFileLocation &partial_remote, int64 ready_size) override {
      send_closure(actor_id_, &FileLoadManager::on_partial_upload, partial_remote, ready_size);
    }
    void on_ok(FileType file_type, const PartialRemoteFileLocation &partial_remote, int64 size) override {
      send_closure(std::move(actor_id_), &FileLoadManager::on_ok_upload, file_type, partial_remote, size);
    }
    void on_error(Status status) override {
      send_closure(std::move(actor_id_), &FileLoadManager::on_error, std::move(status));
    }
  };
  class FileHashUploaderCallback : public FileHashUploader::Callback {
   public:
    explicit FileHashUploaderCallback(ActorShared<FileLoadManager> actor_id) : actor_id_(std::move(actor_id)) {
    }

   private:
    ActorShared<FileLoadManager> actor_id_;

    void on_ok(const FullRemoteFileLocation &remote) override {
      send_closure(std::move(actor_id_), &FileLoadManager::on_ok_upload_full, remote);
    }
    void on_error(Status status) override {
      send_closure(std::move(actor_id_), &FileLoadManager::on_error, std::move(status));
    }
  };

  class FileFromBytesCallback : public FileFromBytes::Callback {
   public:
    explicit FileFromBytesCallback(ActorShared<FileLoadManager> actor_id) : actor_id_(std::move(actor_id)) {
    }

   private:
    ActorShared<FileLoadManager> actor_id_;

    void on_ok(const FullLocalFileLocation &full_local, int64 size) override {
      send_closure(std::move(actor_id_), &FileLoadManager::on_ok_download, full_local, size, true);
    }
    void on_error(Status status) override {
      send_closure(std::move(actor_id_), &FileLoadManager::on_error, std::move(status));
    }
  };
};

}  // namespace td
