//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileLocation.h"

#include "td/actor/actor.h"

#include "td/utils/Promise.h"
#include "td/utils/Status.h"

#include <map>

namespace td {

class FileGenerateActor;

class FileGenerateCallback {
 public:
  FileGenerateCallback() = default;
  FileGenerateCallback(const FileGenerateCallback &) = delete;
  FileGenerateCallback &operator=(const FileGenerateCallback &) = delete;
  virtual ~FileGenerateCallback() = default;

  virtual void on_partial_generate(PartialLocalFileLocation partial_local, int64 expected_size) = 0;
  virtual void on_ok(FullLocalFileLocation local) = 0;
  virtual void on_error(Status error) = 0;
};

class FileGenerateManager final : public Actor {
 public:
  explicit FileGenerateManager(ActorShared<> parent) : parent_(std::move(parent)) {
  }

  void generate_file(uint64 query_id, FullGenerateFileLocation generate_location,
                     const LocalFileLocation &local_location, string name, unique_ptr<FileGenerateCallback> callback);
  void cancel(uint64 query_id);

  // external updates about file generation state
  void external_file_generate_write_part(uint64 query_id, int64 offset, string data, Promise<> promise);
  void external_file_generate_progress(uint64 query_id, int64 expected_size, int64 local_prefix_size,
                                       Promise<> promise);
  void external_file_generate_finish(uint64 query_id, Status status, Promise<> promise);

 private:
  struct Query {
    Query() = default;
    Query(const Query &other) = delete;
    Query &operator=(const Query &other) = delete;
    Query(Query &&other) noexcept;
    Query &operator=(Query &&other) noexcept;
    ~Query();

    ActorOwn<FileGenerateActor> worker_;
  };

  ActorShared<> parent_;
  std::map<uint64, Query> query_id_to_query_;
  bool close_flag_ = false;

  void hangup() final;
  void hangup_shared() final;
  void loop() final;
  void do_cancel(uint64 query_id);
};

}  // namespace td
