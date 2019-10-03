//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/SchedulerLocalStorage.h"

#include "td/db/DbKey.h"
#include "td/db/SqliteDb.h"

#include "td/utils/common.h"

namespace td {

class SqliteConnectionSafe {
 public:
  SqliteConnectionSafe() = default;
  explicit SqliteConnectionSafe(string name, DbKey key = DbKey::empty());

  SqliteDb &get();

  void close();

  void close_and_destroy();

 private:
  string name_;
  LazySchedulerLocalStorage<SqliteDb> lsls_connection_;
};

}  // namespace td
