//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"

#include "td/telegram/files/FileLoader.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileType.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/Status.h"

namespace td {
class FileFromBytes : public FileLoaderActor {
 public:
  class Callback {
   public:
    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    virtual ~Callback() = default;
    virtual void on_ok(const FullLocalFileLocation &full_local, int64 size) = 0;
    virtual void on_error(Status status) = 0;
  };

  FileFromBytes(FileType type, BufferSlice bytes, string name, unique_ptr<Callback> callback);

  // Should just implement all parent pure virtual methods.
  // Must not call any of them...
 private:
  FileType type_;
  BufferSlice bytes_;
  string name_;

  unique_ptr<Callback> callback_;

  FileFd fd_;
  string path_;

  void wakeup() override;
  void set_resource_manager(ActorShared<ResourceManager>) override {
  }
  void update_priority(int8 priority) override {
  }
  void update_resources(const ResourceState &other) override {
  }
};
}  // namespace td
