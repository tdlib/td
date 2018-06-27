//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/files/FileGcWorker.h"
#include "td/telegram/files/FileStats.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {
class FileStatsWorker;
class FileGcWorker;
}  // namespace td

namespace td {

class StorageManager : public Actor {
 public:
  StorageManager(ActorShared<> parent, int32 scheduler_id);
  void get_storage_stats(int32 dialog_limit, Promise<FileStats> promise);
  void get_storage_stats_fast(Promise<FileStatsFast> promise);
  void run_gc(FileGcParameters parameters, Promise<FileStats> promise);
  void update_use_storage_optimizer();
  void on_new_file(int64 size);

 private:
  static constexpr uint32 GC_EACH = 60 * 60 * 24;  // 1 day
  static constexpr uint32 GC_DELAY = 60;
  static constexpr uint32 GC_RAND_DELAY = 60 * 15;

  ActorShared<> parent_;

  int32 scheduler_id_;

  // get stats
  ActorOwn<FileStatsWorker> stats_worker_;
  std::vector<Promise<FileStats>> pending_storage_stats_;
  int32 stats_dialog_limit_ = 0;

  FileTypeStat fast_stat_;

  void on_file_stats(Result<FileStats> r_file_stats, bool dummy);
  void create_stats_worker();
  void send_stats(FileStats &&stats, int32 dialog_limit, std::vector<Promise<FileStats>> promises);

  void save_fast_stat();
  void load_fast_stat();
  static int64 get_db_size();

  // RefCnt
  int32 ref_cnt_{1};
  ActorShared<> create_reference();
  void start_up() override;
  void hangup_shared() override;
  void hangup() override;

  // Gc
  ActorOwn<FileGcWorker> gc_worker_;
  std::vector<Promise<FileStats>> pending_run_gc_;
  FileGcParameters gc_parameters_;

  uint32 last_gc_timestamp_ = 0;
  double next_gc_at_ = 0;

  void on_all_files(Result<FileStats> r_file_stats, bool dummy);
  void create_gc_worker();
  void on_gc_finished(Result<FileStats> r_file_stats, bool dummy);

  uint32 load_last_gc_timestamp();
  void save_last_gc_timestamp();
  void schedule_next_gc();

  void timeout_expired() override;
};

}  // namespace td
