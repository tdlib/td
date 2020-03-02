//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/files/FileGcParameters.h"
#include "td/telegram/files/FileStats.h"

#include "td/utils/CancellationToken.h"
#include "td/utils/logging.h"

namespace td {

extern int VERBOSITY_NAME(file_gc);

struct FileGcResult {
  FileStats kept_file_stats_;
  FileStats removed_file_stats_;
};

class FileGcWorker : public Actor {
 public:
  FileGcWorker(ActorShared<> parent, CancellationToken token) : parent_(std::move(parent)), token_(std::move(token)) {
  }
  void run_gc(const FileGcParameters &parameters, std::vector<FullFileInfo> files, Promise<FileGcResult> promise);

 private:
  ActorShared<> parent_;
  CancellationToken token_;
};

}  // namespace td
