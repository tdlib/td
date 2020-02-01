//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/PromiseFuture.h"

#include "td/db/binlog/BinlogHelper.h"

#include "td/telegram/Global.h"
#include "td/telegram/TdDb.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"

namespace td {

inline Promise<Unit> get_erase_logevent_promise(uint64 logevent_id, Promise<Unit> promise = Promise<Unit>()) {
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

template <class StorerT>
void store_time(double time_at, StorerT &storer) {
  if (time_at == 0) {
    store(-1.0, storer);
  } else {
    double time_left = max(time_at - Time::now(), 0.0);
    store(time_left, storer);
    store(get_server_time(), storer);
  }
}

template <class ParserT>
void parse_time(double &time_at, ParserT &parser) {
  double time_left;
  parse(time_left, parser);
  if (time_left < -0.1) {
    time_at = 0;
  } else {
    double old_server_time;
    parse(old_server_time, parser);
    double passed_server_time = max(parser.context()->server_time() - old_server_time, 0.0);
    time_left = max(time_left - passed_server_time, 0.0);
    time_at = Time::now_cached() + time_left;
  }
}

}  // namespace td
