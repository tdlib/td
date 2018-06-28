//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/PromiseFuture.h"

#include "td/db/binlog/BinlogHelper.h"

#include "td/telegram/Global.h"
#include "td/telegram/TdDb.h"

#include "td/utils/Status.h"

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

}  // namespace td
