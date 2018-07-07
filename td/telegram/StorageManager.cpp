//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StorageManager.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileGcWorker.h"
#include "td/telegram/files/FileStatsWorker.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/port/Stat.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"

namespace td {

StorageManager::StorageManager(ActorShared<> parent, int32 scheduler_id)
    : parent_(std::move(parent)), scheduler_id_(scheduler_id) {
}

void StorageManager::start_up() {
  load_last_gc_timestamp();
  schedule_next_gc();

  load_fast_stat();
}
void StorageManager::on_new_file(int64 size) {
  if (size > 0) {
    fast_stat_.cnt++;
  } else {
    fast_stat_.cnt--;
  }
  fast_stat_.size += size;

  if (fast_stat_.cnt < 0 || fast_stat_.size < 0) {
    LOG(ERROR) << "Wrong fast stat after adding size " << size;
    fast_stat_ = FileTypeStat();
  }
  save_fast_stat();
}
void StorageManager::get_storage_stats(int32 dialog_limit, Promise<FileStats> promise) {
  if (pending_storage_stats_.size() != 0) {
    promise.set_error(Status::Error(400, "Another storage stats is active"));
    return;
  }
  stats_dialog_limit_ = dialog_limit;
  pending_storage_stats_.emplace_back(std::move(promise));

  create_stats_worker();
  send_closure(stats_worker_, &FileStatsWorker::get_stats, false /*need_all_files*/, stats_dialog_limit_ != 0,
               PromiseCreator::lambda([actor_id = actor_id(this)](Result<FileStats> file_stats) {
                 send_closure(actor_id, &StorageManager::on_file_stats, std::move(file_stats), false);
               }));
}

void StorageManager::get_storage_stats_fast(Promise<FileStatsFast> promise) {
  promise.set_value(FileStatsFast(fast_stat_.size, fast_stat_.cnt, get_db_size()));
}

void StorageManager::update_use_storage_optimizer() {
  schedule_next_gc();
}

void StorageManager::run_gc(FileGcParameters parameters, Promise<FileStats> promise) {
  if (pending_run_gc_.size() != 0) {
    promise.set_error(Status::Error(400, "Another gc is active"));
    return;
  }

  pending_run_gc_.emplace_back(std::move(promise));
  if (pending_run_gc_.size() > 1) {
    return;
  }

  gc_parameters_ = std::move(parameters);

  create_stats_worker();
  send_closure(stats_worker_, &FileStatsWorker::get_stats, true /*need_all_file*/,
               !gc_parameters_.owner_dialog_ids.empty() || !gc_parameters_.exclude_owner_dialog_ids.empty() ||
                   gc_parameters_.dialog_limit != 0 /*split_by_owner_dialog_id*/,
               PromiseCreator::lambda([actor_id = actor_id(this)](Result<FileStats> file_stats) {
                 send_closure(actor_id, &StorageManager::on_all_files, std::move(file_stats), false);
               }));
}

void StorageManager::on_file_stats(Result<FileStats> r_file_stats, bool dummy) {
  if (r_file_stats.is_error()) {
    auto promises = std::move(pending_storage_stats_);
    for (auto &promise : promises) {
      promise.set_error(r_file_stats.error().clone());
    }
    return;
  }

  send_stats(r_file_stats.move_as_ok(), stats_dialog_limit_, std::move(pending_storage_stats_));
}

void StorageManager::create_stats_worker() {
  if (stats_worker_.empty()) {
    stats_worker_ = create_actor_on_scheduler<FileStatsWorker>("FileStatsWorker", scheduler_id_, create_reference());
  }
}

void StorageManager::on_all_files(Result<FileStats> r_file_stats, bool dummy) {
  if (r_file_stats.is_error()) {
    LOG(ERROR) << "Stats for GC failed: " << r_file_stats.error();
    auto promises = std::move(pending_run_gc_);
    for (auto &promise : promises) {
      promise.set_error(r_file_stats.error().clone());
    }
    return;
  }

  create_gc_worker();

  send_closure(gc_worker_, &FileGcWorker::run_gc, gc_parameters_, r_file_stats.move_as_ok().all_files,
               PromiseCreator::lambda([actor_id = actor_id(this)](Result<FileStats> r_file_stats) {
                 send_closure(actor_id, &StorageManager::on_gc_finished, std::move(r_file_stats), false);
               }));
}

int64 StorageManager::get_db_size() {
  int64 size = 0;
  auto add_path = [&](CSlice path) {
    TRY_RESULT(info, stat(path));
    size += info.size_;

    return Status::OK();
  };

  G()->td_db()->with_db_path([&](CSlice path) { add_path(path).ignore(); });
  add_path(PSLICE() << G()->parameters().database_directory << "log").ignore();
  add_path(PSLICE() << G()->parameters().database_directory << "log.old").ignore();
  return size;
}

void StorageManager::create_gc_worker() {
  if (gc_worker_.empty()) {
    gc_worker_ = create_actor_on_scheduler<FileGcWorker>("FileGcWorker", scheduler_id_, create_reference());
  }
}

void StorageManager::on_gc_finished(Result<FileStats> r_file_stats, bool dummy) {
  if (r_file_stats.is_error()) {
    LOG(ERROR) << "GC failed: " << r_file_stats.error();
    auto promises = std::move(pending_run_gc_);
    for (auto &promise : promises) {
      promise.set_error(r_file_stats.error().clone());
    }
    return;
  }

  send_stats(r_file_stats.move_as_ok(), gc_parameters_.dialog_limit, std::move(pending_run_gc_));
}

void StorageManager::save_fast_stat() {
  G()->td_db()->get_binlog_pmc()->set("fast_file_stat", log_event_store(fast_stat_).as_slice().str());
}
void StorageManager::load_fast_stat() {
  auto status = log_event_parse(fast_stat_, G()->td_db()->get_binlog_pmc()->get("fast_file_stat"));
  if (status.is_error()) {
    fast_stat_ = FileTypeStat();
  }
}

void StorageManager::send_stats(FileStats &&stats, int32 dialog_limit, std::vector<Promise<FileStats>> promises) {
  fast_stat_ = stats.get_total_nontemp_stat();
  save_fast_stat();

  stats.apply_dialog_limit(dialog_limit);
  std::vector<DialogId> dialog_ids = stats.get_dialog_ids();

  auto promise =
      PromiseCreator::lambda([promises = std::move(promises), stats = std::move(stats)](Result<Unit>) mutable {
        for (auto &promise : promises) {
          promise.set_value(FileStats(stats));
        }
      });

  send_closure(G()->messages_manager(), &MessagesManager::load_dialogs, std::move(dialog_ids), std::move(promise));
}

ActorShared<> StorageManager::create_reference() {
  return actor_shared(this, 1);
}

void StorageManager::hangup_shared() {
  ref_cnt_--;
  if (ref_cnt_ == 0) {
    stop();
  }
}

void StorageManager::hangup() {
  hangup_shared();
}

uint32 StorageManager::load_last_gc_timestamp() {
  last_gc_timestamp_ = to_integer<uint32>(G()->td_db()->get_binlog_pmc()->get("files_gc_ts"));
  return last_gc_timestamp_;
}
void StorageManager::save_last_gc_timestamp() {
  last_gc_timestamp_ = static_cast<uint32>(Clocks::system());
  G()->td_db()->get_binlog_pmc()->set("files_gc_ts", to_string(last_gc_timestamp_));
}
void StorageManager::schedule_next_gc() {
  if (!G()->shared_config().get_option_boolean("use_storage_optimizer") &&
      !G()->parameters().enable_storage_optimizer) {
    next_gc_at_ = 0;
    cancel_timeout();
    LOG(INFO) << "No next file gc is scheduled";
    return;
  }
  auto sys_time = static_cast<uint32>(Clocks::system());

  auto next_gc_at = last_gc_timestamp_ + GC_EACH;
  if (next_gc_at < sys_time) {
    next_gc_at = sys_time;
  }
  if (next_gc_at > sys_time + GC_EACH) {
    next_gc_at = sys_time + GC_EACH;
  }
  next_gc_at += Random::fast(GC_DELAY, GC_DELAY + GC_RAND_DELAY);
  CHECK(next_gc_at >= sys_time);
  auto next_gc_in = next_gc_at - sys_time;

  LOG(INFO) << "Schedule next file gc in " << next_gc_in;
  next_gc_at_ = Time::now() + next_gc_in;
  set_timeout_at(next_gc_at_);
}

void StorageManager::timeout_expired() {
  if (next_gc_at_ == 0) {
    return;
  }
  next_gc_at_ = 0;
  run_gc({}, PromiseCreator::lambda([actor_id = actor_id(this)](Result<FileStats> r_stats) {
           if (!r_stats.is_error() || r_stats.error().code() != 1) {
             send_closure(actor_id, &StorageManager::save_last_gc_timestamp);
           }
           send_closure(actor_id, &StorageManager::schedule_next_gc);
         }));
}

}  // namespace td
