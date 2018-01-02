//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/files/FileGcParameters.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileStats.h"

namespace td {
class FileGcWorker : public Actor {
 public:
  explicit FileGcWorker(ActorShared<> parent) : parent_(std::move(parent)) {
  }
  void run_gc(const FileGcParameters &parameters, std::vector<FullFileInfo> files, Promise<FileStats> promise);

 private:
  ActorShared<> parent_;
  void do_remove_file(const FullFileInfo &info);
};

}  // namespace td
