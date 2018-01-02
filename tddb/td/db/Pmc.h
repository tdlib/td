//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#include "td/db/BinlogKeyValue.h"
#include "td/db/SqliteKeyValue.h"

#include "td/utils/common.h"

#include <memory>

namespace td {
class SqliteKeyValue;
using BinlogPmcBase = BinlogKeyValue<ConcurrentBinlog>;
using BinlogPmc = std::shared_ptr<BinlogPmcBase>;
using BinlogPmcPtr = BinlogPmcBase *;

using BigPmcBase = SqliteKeyValue;
using BigPmc = std::unique_ptr<BigPmcBase>;
using BigPmcPtr = BigPmcBase *;
};  // namespace td
