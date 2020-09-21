//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/logevent/LogEventHelper.h"

#include "td/telegram/Global.h"
#include "td/telegram/TdDb.h"

#include "tddb/td/db/binlog/BinlogHelper.h"

#include "td/utils/logging.h"

namespace td {

void add_log_event(LogeventIdWithGeneration &logevent_id, const Storer &storer, uint32 type, Slice name) {
  LOG(INFO) << "Save " << name << " to binlog";
  if (logevent_id.logevent_id == 0) {
    logevent_id.logevent_id =
        binlog_add(G()->td_db()->get_binlog(), type, storer);
    LOG(INFO) << "Add " << name << " logevent " << logevent_id.logevent_id;
  } else {
    auto new_logevent_id = binlog_rewrite(G()->td_db()->get_binlog(), logevent_id.logevent_id,
                                          type, storer);
    LOG(INFO) << "Rewrite " << name << " logevent " << logevent_id.logevent_id << " with " << new_logevent_id;
  }
  logevent_id.generation++;
}

void delete_log_event(LogeventIdWithGeneration &logevent_id, uint64 generation, Slice name) {
  LOG(INFO) << "Finish to process " << name << " logevent " << logevent_id.logevent_id << " with generation " << generation;
  if (logevent_id.generation == generation) {
    CHECK(logevent_id.logevent_id != 0);
    LOG(INFO) << "Delete " << name << " logevent " << logevent_id.logevent_id;
    binlog_erase(G()->td_db()->get_binlog(), logevent_id.logevent_id);
    logevent_id.logevent_id = 0;
  }
}

Promise<Unit> get_erase_logevent_promise(uint64 logevent_id, Promise<Unit> promise) {
  if (logevent_id == 0) {
    return promise;
  }

  return PromiseCreator::lambda([logevent_id, promise = std::move(promise)](Result<Unit> result) mutable {
    if (!G()->close_flag()) {
      binlog_erase(G()->td_db()->get_binlog(), logevent_id);
    }

    promise.set_result(std::move(result));
  });
}


}  // namespace td
