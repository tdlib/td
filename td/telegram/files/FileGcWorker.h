// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#pragma once

#include "td/telegram/files/FileGcParameters.h"
#include "td/telegram/files/FileStats.h"

#include "td/actor/actor.h"

#include "td/utils/CancellationToken.h"
#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Promise.h"

#include <atomic>

namespace td {

extern std::atomic<int> VERBOSITY_NAME(file_gc);

struct FileGcResult {
  FileStats kept_file_stats_;
  FileStats removed_file_stats_;
};

class FileGcWorker final : public Actor {
 public:
  FileGcWorker(ActorShared<> parent, CancellationToken token) : parent_(std::move(parent)), token_(std::move(token)) {
  }

  void run_gc(const FileGcParameters &parameters, vector<FullFileInfo> files, bool send_updates,
              Promise<FileGcResult> promise);

 private:
  ActorShared<> parent_;
  CancellationToken token_;
};

}  // namespace td
