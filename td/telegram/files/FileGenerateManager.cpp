//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileGenerateManager.h"

#include "td/telegram/td_api.h"

#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileLoaderUtils.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/Td.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/path.h"
#include "td/utils/Slice.h"

#include <utility>

namespace td {

class FileGenerateActor : public Actor {
 public:
  FileGenerateActor() = default;
  FileGenerateActor(const FileGenerateActor &) = delete;
  FileGenerateActor &operator=(const FileGenerateActor &) = delete;
  FileGenerateActor(FileGenerateActor &&) = delete;
  FileGenerateActor &operator=(FileGenerateActor &&) = delete;
  ~FileGenerateActor() override = default;
  virtual void file_generate_progress(int32 expected_size, int32 local_prefix_size, Promise<> promise) = 0;
  virtual void file_generate_finish(Status status, Promise<> promise) = 0;
};

class FileDownloadGenerateActor : public FileGenerateActor {
 public:
  FileDownloadGenerateActor(FileType file_type, FileId file_id, std::unique_ptr<FileGenerateCallback> callback,
                            ActorShared<> parent)
      : file_type_(file_type), file_id_(file_id), callback_(std::move(callback)), parent_(std::move(parent)) {
  }
  void file_generate_progress(int32 expected_size, int32 local_prefix_size, Promise<> promise) override {
    UNREACHABLE();
  }
  void file_generate_finish(Status status, Promise<> promise) override {
    UNREACHABLE();
  }

 private:
  FileType file_type_;
  FileId file_id_;
  std::unique_ptr<FileGenerateCallback> callback_;
  ActorShared<> parent_;

  void start_up() override {
    LOG(INFO) << "DOWNLOAD " << file_id_;
    class Callback : public FileManager::DownloadCallback {
     public:
      explicit Callback(ActorId<FileDownloadGenerateActor> parent) : parent_(std::move(parent)) {
      }

      // TODO: upload during download

      void on_download_ok(FileId file_id) override {
        send_closure(parent_, &FileDownloadGenerateActor::on_download_ok);
      }
      void on_download_error(FileId file_id, Status error) override {
        send_closure(parent_, &FileDownloadGenerateActor::on_download_error, std::move(error));
      }

     private:
      ActorId<FileDownloadGenerateActor> parent_;
    };

    send_closure(G()->file_manager(), &FileManager::download, file_id_, std::make_unique<Callback>(actor_id(this)), 1);
  }
  void hangup() override {
    send_closure(G()->file_manager(), &FileManager::download, file_id_, nullptr, 0);
    stop();
  }

  void on_download_ok() {
    send_lambda(G()->file_manager(), [file_type = file_type_, file_id = file_id_, callback = std::move(callback_)] {
      auto file_view = G()->td().get_actor_unsafe()->file_manager_->get_file_view(file_id);
      if (file_view.has_local_location()) {
        auto location = file_view.local_location();
        location.file_type_ = file_type;
        callback->on_ok(location);
      } else {
        LOG(ERROR) << "Expected to have local location";
        callback->on_error(Status::Error("Unknown"));
      }
    });
    stop();
  }
  void on_download_error(Status error) {
    callback_->on_error(std::move(error));
    stop();
  }
};

class FileExternalGenerateActor : public FileGenerateActor {
 public:
  FileExternalGenerateActor(uint64 query_id, const FullGenerateFileLocation &generate_location,
                            const LocalFileLocation &local_location, string name,
                            std::unique_ptr<FileGenerateCallback> callback, ActorShared<> parent)
      : query_id_(query_id)
      , generate_location_(generate_location)
      , local_(local_location)
      , name_(std::move(name))
      , callback_(std::move(callback))
      , parent_(std::move(parent)) {
  }

  void file_generate_progress(int32 expected_size, int32 local_prefix_size, Promise<> promise) override {
    check_status(do_file_generate_progress(expected_size, local_prefix_size), std::move(promise));
  }
  void file_generate_finish(Status status, Promise<> promise) override {
    check_status(do_file_generate_finish(std::move(status)), std::move(promise));
  }

 private:
  uint64 query_id_;
  FullGenerateFileLocation generate_location_;
  LocalFileLocation local_;
  string name_;
  string path_;
  std::unique_ptr<FileGenerateCallback> callback_;
  ActorShared<> parent_;

  void start_up() override {
    if (local_.type() == LocalFileLocation::Type::Full) {
      callback_->on_ok(local_.full());
      callback_.reset();
      return stop();
    }

    if (local_.type() == LocalFileLocation::Type::Partial) {
      const auto &partial = local_.partial();
      path_ = partial.path_;
      LOG(INFO) << "Unlink partially generated file at " << path_;
      unlink(path_).ignore();
    } else {
      auto r_file_path = open_temp_file(generate_location_.file_type_);
      if (r_file_path.is_error()) {
        return check_status(r_file_path.move_as_error());
      }
      auto file_path = r_file_path.move_as_ok();
      file_path.first.close();
      path_ = file_path.second;
    }
    send_closure(
        G()->td(), &Td::send_update,
        make_tl_object<td_api::updateFileGenerationStart>(
            static_cast<int64>(query_id_), generate_location_.original_path_, path_, generate_location_.conversion_));
  }
  void hangup() override {
    check_status(Status::Error(1, "Cancelled"));
  }

  Status do_file_generate_progress(int32 expected_size, int32 local_prefix_size) {
    if (local_prefix_size < 0) {
      return Status::Error(1, "Invalid local prefix size");
    }
    callback_->on_partial_generate(
        PartialLocalFileLocation{generate_location_.file_type_, path_, 1, local_prefix_size, ""}, expected_size);
    return Status::OK();
  }

  Status do_file_generate_finish(Status status) {
    TRY_STATUS(std::move(status));

    auto dir = get_files_dir(generate_location_.file_type_);

    TRY_RESULT(perm_path, create_from_temp(path_, dir, name_));
    callback_->on_ok(FullLocalFileLocation(generate_location_.file_type_, std::move(perm_path), 0));
    callback_.reset();
    stop();
    return Status::OK();
  }

  void check_status(Status status, Promise<> promise = Promise<>()) {
    if (promise) {
      if (status.is_ok() || status.code() == 1) {
        promise.set_value(Unit());
      } else {
        promise.set_error(Status::Error(400, status.message()));
      }
    }

    if (status.is_error()) {
      LOG(INFO) << "Unlink partially generated file at " << path_ << " because of " << status;
      unlink(path_).ignore();
      callback_->on_error(std::move(status));
      callback_.reset();
      stop();
    }
  }

  void tear_down() override {
    send_closure(G()->td(), &Td::send_update,
                 make_tl_object<td_api::updateFileGenerationStop>(static_cast<int64>(query_id_)));
  }
};

FileGenerateManager::Query::~Query() = default;
FileGenerateManager::Query::Query(Query &&other) = default;
FileGenerateManager::Query &FileGenerateManager::Query::operator=(Query &&other) = default;

void FileGenerateManager::generate_file(uint64 query_id, const FullGenerateFileLocation &generate_location,
                                        const LocalFileLocation &local_location, string name,
                                        std::unique_ptr<FileGenerateCallback> callback) {
  CHECK(query_id != 0);
  auto it_flag = query_id_to_query_.insert(std::make_pair(query_id, Query{}));
  CHECK(it_flag.second) << "Query id must be unique";
  auto parent = actor_shared(this, query_id);

  Slice file_id_query = "#file_id#";
  Slice conversion = generate_location.conversion_;

  auto &query = it_flag.first->second;
  if (conversion.copy().truncate(file_id_query.size()) == file_id_query) {
    auto file_id = FileId(to_integer<int32>(conversion.substr(file_id_query.size())));
    query.worker_ = create_actor<FileDownloadGenerateActor>("FileDownloadGenerateActor", generate_location.file_type_,
                                                            file_id, std::move(callback), std::move(parent));
  } else {
    query.worker_ = create_actor<FileExternalGenerateActor>("FileExternalGenerationActor", query_id, generate_location,
                                                            local_location, std::move(name), std::move(callback),
                                                            std::move(parent));
  }
}

void FileGenerateManager::cancel(uint64 query_id) {
  auto it = query_id_to_query_.find(query_id);
  if (it == query_id_to_query_.end()) {
    return;
  }
  it->second.worker_.reset();
}

void FileGenerateManager::external_file_generate_progress(uint64 query_id, int32 expected_size, int32 local_prefix_size,
                                                          Promise<> promise) {
  auto it = query_id_to_query_.find(query_id);
  if (it == query_id_to_query_.end()) {
    return promise.set_error(Status::Error(400, "Unknown generation_id"));
  }
  send_closure(it->second.worker_, &FileGenerateActor::file_generate_progress, expected_size, local_prefix_size,
               std::move(promise));
}

void FileGenerateManager::external_file_generate_finish(uint64 query_id, Status status, Promise<> promise) {
  auto it = query_id_to_query_.find(query_id);
  if (it == query_id_to_query_.end()) {
    return promise.set_error(Status::Error(400, "Unknown generation_id"));
  }
  send_closure(it->second.worker_, &FileGenerateActor::file_generate_finish, std::move(status), std::move(promise));
}

void FileGenerateManager::do_cancel(uint64 query_id) {
  query_id_to_query_.erase(query_id);
}

void FileGenerateManager::hangup_shared() {
  do_cancel(get_link_token());
  loop();
}

void FileGenerateManager::hangup() {
  close_flag_ = true;
  for (auto &it : query_id_to_query_) {
    it.second.worker_.reset();
  }
  loop();
}

void FileGenerateManager::loop() {
  if (close_flag_ && query_id_to_query_.empty()) {
    stop();
  }
}

}  // namespace td
